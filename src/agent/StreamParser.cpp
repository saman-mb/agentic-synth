#include "agent/StreamParser.h"

#include <algorithm>

#include <juce_core/juce_core.h>

namespace agentic_synth::agent {

namespace {

// ── juce::var field accessors ───────────────────────────────────────────────
// Each LLM-streamed top-level field is parsed by juce::JSON::parse into a
// juce::var.  These helpers replace the prior hand-rolled substring scans
// (extractFloat/Int/ObjectAt) with type-checked lookups that fall back when
// the field is missing or the wrong shape — robust against the malformed
// fragments LLMs occasionally emit.

float varF(const juce::var& obj, const char* key, float fallback) noexcept {
    if (!obj.isObject())
        return fallback;
    const juce::var v = obj.getProperty(juce::Identifier(key), juce::var());
    if (v.isDouble() || v.isInt() || v.isInt64())
        return static_cast<float>(static_cast<double>(v));
    return fallback;
}

int varI(const juce::var& obj, const char* key, int fallback) noexcept {
    if (!obj.isObject())
        return fallback;
    const juce::var v = obj.getProperty(juce::Identifier(key), juce::var());
    if (v.isInt() || v.isInt64() || v.isDouble())
        return static_cast<int>(static_cast<juce::int64>(v));
    if (v.isBool())
        return v ? 1 : 0;
    return fallback;
}

} // namespace

void StreamParser::reset() {
    state_ = State::Idle;
    depth_ = 0;
    inString_ = false;
    escape_ = false;
    currentKey_.clear();
    currentValue_.clear();
    partial_ = make_default_patch();
    done_ = false;
}

void StreamParser::feedChunk(std::string_view chunk) {
    for (char c : chunk) {
        if (done_)
            break;
        processChar(c);
    }
}

void StreamParser::processChar(char c) {
    switch (state_) {
    case State::Idle:
        if (c == '{')
            state_ = State::TopLevel;
        break;

    case State::TopLevel:
        if (c == '"') {
            state_ = State::InKey;
            currentKey_.clear();
        } else if (c == '}') {
            done_ = true;
        }
        // skip whitespace and commas between fields
        break;

    case State::InKey:
        if (escape_) {
            currentKey_ += c;
            escape_ = false;
        } else if (c == '\\') {
            escape_ = true;
        } else if (c == '"') {
            state_ = State::AfterColon;
        } else {
            currentKey_ += c;
        }
        break;

    case State::AfterColon:
        if (c == ':') {
            state_ = State::InValue;
            currentValue_.clear();
            depth_ = 0;
            inString_ = false;
            escape_ = false;
        }
        break;

    case State::InValue:
        if (inString_) {
            if (escape_) {
                currentValue_ += c;
                escape_ = false;
            } else if (c == '\\') {
                escape_ = true;
                currentValue_ += c;
            } else if (c == '"') {
                inString_ = false;
                currentValue_ += c;
            } else {
                currentValue_ += c;
            }
        } else {
            if (c == '"') {
                inString_ = true;
                currentValue_ += c;
            } else if (c == '{' || c == '[') {
                ++depth_;
                currentValue_ += c;
            } else if (c == '}' || c == ']') {
                --depth_;
                if (depth_ < 0) {
                    // Root object closing; finalize pending scalar value.
                    onFieldComplete();
                    done_ = true;
                } else {
                    currentValue_ += c;
                    if (depth_ == 0) {
                        // Object or array value is complete.
                        onFieldComplete();
                        state_ = State::TopLevel;
                    }
                }
            } else if (c == ',' && depth_ == 0) {
                // Scalar value ended by comma.
                onFieldComplete();
                state_ = State::TopLevel;
            } else {
                currentValue_ += c;
            }
        }
        break;
    }
}

void StreamParser::onFieldComplete() {
    if (currentKey_.empty()) {
        currentKey_.clear();
        currentValue_.clear();
        return;
    }
    const auto first = currentValue_.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        currentKey_.clear();
        currentValue_.clear();
        return;
    }
    const auto last = currentValue_.find_last_not_of(" \t\r\n");
    const std::string trimmed = currentValue_.substr(first, last - first + 1);

    // Wrap the completed top-level field as a one-key JSON fragment and hand
    // it to juce::JSON::parse.  This is off the audio thread (LLM streaming
    // thread), so the per-field allocation is fine — and it eliminates the
    // entire class of substring-scan bugs the old extractFloat/Int helpers
    // suffered from.
    const juce::String fragment = "{\"" + juce::String(currentKey_) + "\":" + juce::String(trimmed) + "}";

    juce::var parsed;
    const auto result = juce::JSON::parse(fragment, parsed);
    if (result.wasOk() && parsed.isObject())
        applyField(currentKey_, parsed);

    if (callback_)
        callback_(partial_);
    currentKey_.clear();
    currentValue_.clear();
}

void StreamParser::applyField(const std::string& key, const juce::var& wrapper) {
    // wrapper is {"<key>": <value>}; pull the value out once.
    const juce::var val = wrapper.getProperty(juce::Identifier(key), juce::var());

    if (key == "master_gain") {
        if (val.isDouble() || val.isInt() || val.isInt64())
            partial_.master_gain = std::clamp(static_cast<float>(static_cast<double>(val)), 0.0f, 1.0f);
    } else if (key == "portamento_s") {
        if (val.isDouble() || val.isInt() || val.isInt64())
            partial_.portamento_s = std::clamp(static_cast<float>(static_cast<double>(val)), 0.0f, 5.0f);
    } else if (key == "voice_count") {
        if (val.isInt() || val.isInt64() || val.isDouble())
            partial_.voice_count =
                static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<juce::int64>(val)), 1, 16));
    } else if (key == "patch_id") {
        if (val.isInt() || val.isInt64() || val.isDouble())
            partial_.patch_id = static_cast<uint32_t>(static_cast<juce::int64>(val));
    } else if (key == "filter") {
        partial_.filter.cutoff_hz = std::clamp(varF(val, "cutoff_hz", partial_.filter.cutoff_hz), 20.0f, 20000.0f);
        partial_.filter.resonance = std::clamp(varF(val, "resonance", partial_.filter.resonance), 0.0f, 1.0f);
        partial_.filter.env_mod = std::clamp(varF(val, "env_mod", partial_.filter.env_mod), -1.0f, 1.0f);
        partial_.filter.key_track = std::clamp(varF(val, "key_track", partial_.filter.key_track), 0.0f, 1.0f);
        partial_.filter.drive = std::clamp(varF(val, "drive", partial_.filter.drive), 0.0f, 1.0f);
        const int t = varI(val, "type", static_cast<int>(partial_.filter.type));
        partial_.filter.type = static_cast<FilterType>(std::clamp(t, 0, 4));
    } else if (key == "filter_env") {
        partial_.filter_env.attack_s = std::clamp(varF(val, "attack_s", partial_.filter_env.attack_s), 0.0f, 10.0f);
        partial_.filter_env.decay_s = std::clamp(varF(val, "decay_s", partial_.filter_env.decay_s), 0.0f, 10.0f);
        partial_.filter_env.sustain = std::clamp(varF(val, "sustain", partial_.filter_env.sustain), 0.0f, 1.0f);
        partial_.filter_env.release_s = std::clamp(varF(val, "release_s", partial_.filter_env.release_s), 0.0f, 20.0f);
    } else if (key == "amp_env") {
        partial_.amp_env.attack_s = std::clamp(varF(val, "attack_s", partial_.amp_env.attack_s), 0.0f, 10.0f);
        partial_.amp_env.decay_s = std::clamp(varF(val, "decay_s", partial_.amp_env.decay_s), 0.0f, 10.0f);
        partial_.amp_env.sustain = std::clamp(varF(val, "sustain", partial_.amp_env.sustain), 0.0f, 1.0f);
        partial_.amp_env.release_s = std::clamp(varF(val, "release_s", partial_.amp_env.release_s), 0.0f, 20.0f);
    } else if (key == "reverb") {
        partial_.reverb.size = std::clamp(varF(val, "size", partial_.reverb.size), 0.0f, 1.0f);
        partial_.reverb.damping = std::clamp(varF(val, "damping", partial_.reverb.damping), 0.0f, 1.0f);
        partial_.reverb.width = std::clamp(varF(val, "width", partial_.reverb.width), 0.0f, 1.0f);
        partial_.reverb.mix = std::clamp(varF(val, "mix", partial_.reverb.mix), 0.0f, 1.0f);
    } else if (key == "delay") {
        partial_.delay.time_s = std::clamp(varF(val, "time_s", partial_.delay.time_s), 0.0f, 2.0f);
        partial_.delay.feedback = std::clamp(varF(val, "feedback", partial_.delay.feedback), 0.0f, 0.99f);
        partial_.delay.mix = std::clamp(varF(val, "mix", partial_.delay.mix), 0.0f, 1.0f);
        partial_.delay.stereo = std::clamp(varF(val, "stereo", partial_.delay.stereo), 0.0f, 1.0f);
    } else if (key == "osc") {
        if (const auto* arr = val.getArray()) {
            const int n = std::min(arr->size(), kMaxOscillators);
            for (int i = 0; i < n; ++i) {
                const juce::var& entry = arr->getReference(i);
                if (!entry.isObject())
                    continue;
                auto& o = partial_.osc[i];
                const int t = varI(entry, "type", static_cast<int>(o.type));
                o.type = static_cast<OscType>(std::clamp(t, 0, 7));
                o.semitone_offset = std::clamp(varF(entry, "semitone_offset", o.semitone_offset), -48.0f, 48.0f);
                o.detune_cents = std::clamp(varF(entry, "detune_cents", o.detune_cents), -100.0f, 100.0f);
                o.wavetable_pos = std::clamp(varF(entry, "wavetable_pos", o.wavetable_pos), 0.0f, 1.0f);
                o.fm_ratio = std::clamp(varF(entry, "fm_ratio", o.fm_ratio), 0.5f, 16.0f);
                o.fm_depth = std::clamp(varF(entry, "fm_depth", o.fm_depth), 0.0f, 1.0f);
                o.volume = std::clamp(varF(entry, "volume", o.volume), 0.0f, 1.0f);
                o.pan = std::clamp(varF(entry, "pan", o.pan), -1.0f, 1.0f);
                o.pulse_width = std::clamp(varF(entry, "pulse_width", o.pulse_width), 0.01f, 0.99f);
                const int en = varI(entry, "enabled", static_cast<int>(o.enabled));
                o.enabled = static_cast<uint8_t>(en != 0 ? 1 : 0);
            }
        }
    } else if (key == "lfo") {
        if (const auto* arr = val.getArray()) {
            const int n = std::min(arr->size(), kMaxLfos);
            for (int i = 0; i < n; ++i) {
                const juce::var& entry = arr->getReference(i);
                if (!entry.isObject())
                    continue;
                auto& l = partial_.lfo[i];
                const int wf = varI(entry, "waveform", static_cast<int>(l.waveform));
                l.waveform = static_cast<LfoWaveform>(std::clamp(wf, 0, 4));
                const int tgt = varI(entry, "target", static_cast<int>(l.target));
                l.target = static_cast<LfoTarget>(std::clamp(tgt, 0, 6));
                l.rate_hz = std::clamp(varF(entry, "rate_hz", l.rate_hz), 0.01f, 20.0f);
                l.depth = std::clamp(varF(entry, "depth", l.depth), 0.0f, 1.0f);
                l.phase_offset = std::clamp(varF(entry, "phase_offset", l.phase_offset), 0.0f, 1.0f);
                const int bs = varI(entry, "bpm_sync", static_cast<int>(l.bpm_sync));
                l.bpm_sync = static_cast<uint8_t>(bs != 0 ? 1 : 0);
            }
        }
    }
}

} // namespace agentic_synth::agent
