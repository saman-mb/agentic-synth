#include "agent/StreamParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace agentic_synth::agent {

namespace {

float extractFloat(const std::string& json, const char* key, float fallback) noexcept {
    std::string pat = std::string("\"") + key + "\"";
    const auto p = json.find(pat);
    if (p == std::string::npos) return fallback;
    const auto colon = json.find(':', p + pat.size());
    if (colon == std::string::npos) return fallback;
    std::size_t start = colon + 1;
    while (start < json.size() && std::isspace((unsigned char)json[start])) ++start;
    float v = fallback;
    std::sscanf(json.c_str() + start, "%f", &v);
    return v;
}

int extractInt(const std::string& json, const char* key, int fallback) noexcept {
    std::string pat = std::string("\"") + key + "\"";
    const auto p = json.find(pat);
    if (p == std::string::npos) return fallback;
    const auto colon = json.find(':', p + pat.size());
    if (colon == std::string::npos) return fallback;
    std::size_t start = colon + 1;
    while (start < json.size() && std::isspace((unsigned char)json[start])) ++start;
    int v = fallback;
    std::sscanf(json.c_str() + start, "%d", &v);
    return v;
}

// Return substring of the i-th '{...}' block in json.
std::string extractObjectAt(const std::string& json, int idx) {
    int found = 0;
    for (std::size_t i = 0; i < json.size(); ++i) {
        if (json[i] != '{') continue;
        if (found++ < idx) {
            // skip past this block
            int d = 0;
            for (; i < json.size(); ++i) {
                if (json[i] == '{') ++d;
                else if (json[i] == '}' && --d == 0) break;
            }
            continue;
        }
        // found the idx-th block
        int d = 0;
        std::size_t end = i;
        for (; end < json.size(); ++end) {
            if (json[end] == '{') ++d;
            else if (json[end] == '}' && --d == 0) break;
        }
        return json.substr(i, end - i + 1);
    }
    return {};
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
        if (done_) break;
        processChar(c);
    }
}

void StreamParser::processChar(char c) {
    switch (state_) {
    case State::Idle:
        if (c == '{') state_ = State::TopLevel;
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
        if (escape_) { currentKey_ += c; escape_ = false; }
        else if (c == '\\') { escape_ = true; }
        else if (c == '"') { state_ = State::AfterColon; }
        else { currentKey_ += c; }
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
            if (escape_) { currentValue_ += c; escape_ = false; }
            else if (c == '\\') { escape_ = true; currentValue_ += c; }
            else if (c == '"') { inString_ = false; currentValue_ += c; }
            else { currentValue_ += c; }
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
    if (currentKey_.empty()) { currentKey_.clear(); currentValue_.clear(); return; }
    const auto first = currentValue_.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { currentKey_.clear(); currentValue_.clear(); return; }
    const auto last = currentValue_.find_last_not_of(" \t\r\n");
    applyField(currentKey_, currentValue_.substr(first, last - first + 1));
    if (callback_) callback_(partial_);
    currentKey_.clear();
    currentValue_.clear();
}

void StreamParser::applyField(const std::string& key, const std::string& val) {
    if (key == "master_gain") {
        float v; if (std::sscanf(val.c_str(), "%f", &v) == 1) partial_.master_gain = std::clamp(v, 0.0f, 1.0f);
    } else if (key == "portamento_s") {
        float v; if (std::sscanf(val.c_str(), "%f", &v) == 1) partial_.portamento_s = std::clamp(v, 0.0f, 5.0f);
    } else if (key == "voice_count") {
        int v; if (std::sscanf(val.c_str(), "%d", &v) == 1) partial_.voice_count = static_cast<uint8_t>(std::clamp(v, 1, 16));
    } else if (key == "patch_id") {
        unsigned v; if (std::sscanf(val.c_str(), "%u", &v) == 1) partial_.patch_id = v;
    } else if (key == "filter") {
        partial_.filter.cutoff_hz = std::clamp(extractFloat(val, "cutoff_hz", partial_.filter.cutoff_hz), 20.0f, 20000.0f);
        partial_.filter.resonance  = std::clamp(extractFloat(val, "resonance",  partial_.filter.resonance),  0.0f, 1.0f);
        partial_.filter.env_mod    = std::clamp(extractFloat(val, "env_mod",    partial_.filter.env_mod),   -1.0f, 1.0f);
        partial_.filter.key_track  = std::clamp(extractFloat(val, "key_track",  partial_.filter.key_track),  0.0f, 1.0f);
        partial_.filter.drive      = std::clamp(extractFloat(val, "drive",      partial_.filter.drive),      0.0f, 1.0f);
        int t = extractInt(val, "type", static_cast<int>(partial_.filter.type));
        partial_.filter.type = static_cast<FilterType>(std::clamp(t, 0, 4));
    } else if (key == "filter_env") {
        partial_.filter_env.attack_s  = std::clamp(extractFloat(val, "attack_s",  partial_.filter_env.attack_s),  0.0f, 10.0f);
        partial_.filter_env.decay_s   = std::clamp(extractFloat(val, "decay_s",   partial_.filter_env.decay_s),   0.0f, 10.0f);
        partial_.filter_env.sustain   = std::clamp(extractFloat(val, "sustain",   partial_.filter_env.sustain),   0.0f, 1.0f);
        partial_.filter_env.release_s = std::clamp(extractFloat(val, "release_s", partial_.filter_env.release_s), 0.0f, 20.0f);
    } else if (key == "amp_env") {
        partial_.amp_env.attack_s  = std::clamp(extractFloat(val, "attack_s",  partial_.amp_env.attack_s),  0.0f, 10.0f);
        partial_.amp_env.decay_s   = std::clamp(extractFloat(val, "decay_s",   partial_.amp_env.decay_s),   0.0f, 10.0f);
        partial_.amp_env.sustain   = std::clamp(extractFloat(val, "sustain",   partial_.amp_env.sustain),   0.0f, 1.0f);
        partial_.amp_env.release_s = std::clamp(extractFloat(val, "release_s", partial_.amp_env.release_s), 0.0f, 20.0f);
    } else if (key == "reverb") {
        partial_.reverb.size    = std::clamp(extractFloat(val, "size",    partial_.reverb.size),    0.0f, 1.0f);
        partial_.reverb.damping = std::clamp(extractFloat(val, "damping", partial_.reverb.damping), 0.0f, 1.0f);
        partial_.reverb.width   = std::clamp(extractFloat(val, "width",   partial_.reverb.width),   0.0f, 1.0f);
        partial_.reverb.mix     = std::clamp(extractFloat(val, "mix",     partial_.reverb.mix),     0.0f, 1.0f);
    } else if (key == "delay") {
        partial_.delay.time_s   = std::clamp(extractFloat(val, "time_s",   partial_.delay.time_s),   0.0f, 2.0f);
        partial_.delay.feedback = std::clamp(extractFloat(val, "feedback", partial_.delay.feedback), 0.0f, 0.99f);
        partial_.delay.mix      = std::clamp(extractFloat(val, "mix",      partial_.delay.mix),      0.0f, 1.0f);
    } else if (key == "osc") {
        for (int i = 0; i < kMaxOscillators; ++i) {
            const std::string obj = extractObjectAt(val, i);
            if (obj.empty()) break;
            auto& o = partial_.osc[i];
            int t = extractInt(obj, "type", static_cast<int>(o.type));
            o.type           = static_cast<OscType>(std::clamp(t, 0, 7));
            o.semitone_offset = std::clamp(extractFloat(obj, "semitone_offset", o.semitone_offset), -48.0f, 48.0f);
            o.detune_cents    = std::clamp(extractFloat(obj, "detune_cents",    o.detune_cents),   -100.0f, 100.0f);
            o.wavetable_pos   = std::clamp(extractFloat(obj, "wavetable_pos",   o.wavetable_pos),    0.0f, 1.0f);
            o.fm_ratio        = std::clamp(extractFloat(obj, "fm_ratio",        o.fm_ratio),         0.5f, 16.0f);
            o.fm_depth        = std::clamp(extractFloat(obj, "fm_depth",        o.fm_depth),         0.0f, 1.0f);
            o.volume          = std::clamp(extractFloat(obj, "volume",          o.volume),           0.0f, 1.0f);
            o.pan             = std::clamp(extractFloat(obj, "pan",             o.pan),             -1.0f, 1.0f);
            o.pulse_width     = std::clamp(extractFloat(obj, "pulse_width",     o.pulse_width),      0.01f, 0.99f);
            int en = extractInt(obj, "enabled", static_cast<int>(o.enabled));
            o.enabled = static_cast<uint8_t>(en != 0 ? 1 : 0);
        }
    } else if (key == "lfo") {
        for (int i = 0; i < kMaxLfos; ++i) {
            const std::string obj = extractObjectAt(val, i);
            if (obj.empty()) break;
            auto& l = partial_.lfo[i];
            int wf = extractInt(obj, "waveform", static_cast<int>(l.waveform));
            l.waveform    = static_cast<LfoWaveform>(std::clamp(wf, 0, 4));
            int tgt = extractInt(obj, "target", static_cast<int>(l.target));
            l.target      = static_cast<LfoTarget>(std::clamp(tgt, 0, 6));
            l.rate_hz     = std::clamp(extractFloat(obj, "rate_hz",     l.rate_hz),     0.01f, 20.0f);
            l.depth       = std::clamp(extractFloat(obj, "depth",       l.depth),       0.0f, 1.0f);
            l.phase_offset = std::clamp(extractFloat(obj, "phase_offset", l.phase_offset), 0.0f, 1.0f);
            int bs = extractInt(obj, "bpm_sync", static_cast<int>(l.bpm_sync));
            l.bpm_sync = static_cast<uint8_t>(bs != 0 ? 1 : 0);
        }
    }
}

} // namespace agentic_synth::agent
