#include "agent/AgentBridge.h"
#include "agent/WebSocketBridge.h"

#include <fstream>
#include <sstream>
#include <iomanip>

namespace agentic_synth::agent {

AgentBridge::AgentBridge() {
    // Wire stream parser: each completed field injects a partial patch
    // directly onto the audio SPSC queue for < 500 ms first-audible-change.
    streamParser_.setCallback([this](const PatchStruct& p) {
        pipeline_.injectPatch(p);
    });
}

namespace {

// ---------------------------------------------------------------------------
// Minimal JSON field extractors for flat WebSocket message objects.
// These handle the specific formats sent by the React UI — no external library.
// ---------------------------------------------------------------------------

std::string jsStr(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    const auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

float jsFloat(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0f;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0.0f;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return 0.0f;
    try { return std::stof(json.substr(pos)); } catch (...) { return 0.0f; }
}

bool jsBool(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return json.size() > pos + 3 && json.substr(pos, 4) == "true";
}

// ---------------------------------------------------------------------------
// Map a UI param path (e.g. "filter.cutoff_hz") to a PatchDelta with that
// one field set, leaving all other fields as nullopt.
// ---------------------------------------------------------------------------

mapper::PatchDelta paramToDelta(const std::string& param, float value) {
    mapper::PatchDelta d;
    // Filter
    if (param == "filter.cutoff_hz")     { d.filter_cutoff    = value; return d; }
    if (param == "filter.resonance")     { d.filter_resonance = value; return d; }
    if (param == "filter.env_mod")       { d.filter_env_mod   = value; return d; }
    if (param == "filter.drive")         { d.filter_drive     = value; return d; }
    // Amp envelope
    if (param == "amp_env.attack_s")     { d.amp_attack       = value; return d; }
    if (param == "amp_env.decay_s")      { d.amp_decay        = value; return d; }
    if (param == "amp_env.sustain")      { d.amp_sustain      = value; return d; }
    if (param == "amp_env.release_s")    { d.amp_release      = value; return d; }
    // Filter envelope
    if (param == "filter_env.attack_s")  { d.flt_attack       = value; return d; }
    if (param == "filter_env.decay_s")   { d.flt_decay        = value; return d; }
    if (param == "filter_env.sustain")   { d.flt_sustain      = value; return d; }
    if (param == "filter_env.release_s") { d.flt_release      = value; return d; }
    // LFO 0
    if (param == "lfo.0.rate_hz")        { d.lfo0_rate        = value; return d; }
    if (param == "lfo.0.depth")          { d.lfo0_depth       = value; return d; }
    // Reverb
    if (param == "reverb.size")          { d.reverb_size      = value; return d; }
    if (param == "reverb.damping")       { d.reverb_damping   = value; return d; }
    if (param == "reverb.width")         { d.reverb_width     = value; return d; }
    if (param == "reverb.mix")           { d.reverb_mix       = value; return d; }
    // Delay
    if (param == "delay.time_s")         { d.delay_time       = value; return d; }
    if (param == "delay.feedback")       { d.delay_feedback   = value; return d; }
    if (param == "delay.mix")            { d.delay_mix        = value; return d; }
    // Global
    if (param == "master_gain")          { d.master_gain      = value; return d; }
    if (param == "portamento_s")         { d.portamento       = value; return d; }
    // Oscillators (osc.N.field)
    if (param.size() > 4 && param.compare(0, 4, "osc.") == 0) {
        const auto p2  = param.substr(4);
        const auto dot = p2.find('.');
        if (dot != std::string::npos) {
            int idx = 0;
            try { idx = std::stoi(p2.substr(0, dot)); } catch (...) {}
            const std::string field = p2.substr(dot + 1);
            if (idx == 0) {
                if (field == "volume")          { d.osc0_volume      = value; return d; }
                if (field == "detune_cents")    { d.osc0_detune      = value; return d; }
                if (field == "semitone_offset") { d.osc0_semitone    = value; return d; }
                if (field == "fm_ratio")        { d.osc0_fm_ratio    = value; return d; }
                if (field == "fm_depth")        { d.osc0_fm_depth    = value; return d; }
                if (field == "pulse_width")     { d.osc0_pulse_width = value; return d; }
            } else if (idx == 1) {
                if (field == "volume")       { d.osc1_volume = value; return d; }
                if (field == "detune_cents") { d.osc1_detune = value; return d; }
            }
        }
    }
    return d; // unknown param → empty delta (no-op)
}

// Load text file; returns empty string on failure.
std::string load_text_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

std::string AgentBridge::status() const { return "agent-bridge-v2"; }

PatchStruct AgentBridge::submitPrompt(const std::string& prompt) {
    // Reset stream parser so field-complete callbacks from a prior LLM call
    // don't bleed into this submission's heuristic patch.
    streamParser_.reset();
    // Issue #65/#68: heuristic dispatched < 200 ms; semantic layer refines in place.
    PatchStruct patch = pipeline_.submit(prompt);
    if (semanticMapper_.apply(prompt, patch) > 0)
        pipeline_.injectPatch(patch);
    return patch;
}

void AgentBridge::refinePatch(const PatchStruct& llmPatch) {
    pipeline_.refinePatch(llmPatch);
}

std::optional<PatchStruct> AgentBridge::pollPatch() noexcept {
    return pipeline_.poll();
}

std::array<PatchStruct, engine::VariationEngine::kVariationCount>
AgentBridge::generateVariations(const PatchStruct& base) const {
    return variationEngine_.generateVariations(base);
}

std::array<PatchStruct, engine::VariationEngine::kVariationCount>
AgentBridge::generateVariationsWithSeed(const PatchStruct& base, uint32_t perturbSeed) const {
    return variationEngine_.generateVariationsWithSeed(base, perturbSeed);
}

void AgentBridge::recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch) {
    memory_.recordFeedback(kind, prompt, patch);
}

std::string AgentBridge::buildSystemPrompt(const std::string& userPrompt) const {
    const std::string& base = sampler_.systemPrompt().empty()
        ? std::string("You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n")
        : sampler_.systemPrompt();

    std::string prompt = base;

    // Append MIDI CC context so the AI respects the user's current performance state.
    if (midiCutoffNorm_ < 0.25f)
        prompt += "MIDI context: filter is currently closed (dark sound).\n";
    else if (midiCutoffNorm_ > 0.75f)
        prompt += "MIDI context: filter is currently open (bright sound).\n";
    if (midiResonanceNorm_ > 0.5f)
        prompt += "MIDI context: high resonance is active.\n";

    std::string recap = memory_.buildRecap(userPrompt);
    if (recap.empty()) return prompt;
    return prompt + "\n## Session Feedback\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
}

PatchVector AgentBridge::getParameterBias(const std::string& userPrompt) const {
    return memory_.computeParameterBias(userPrompt);
}

std::optional<PatchStruct> AgentBridge::generateLlmPatch(const std::string& prompt,
                                                          uint32_t patch_id) {
    auto result = sampler_.generate(prompt, patch_id);
    if (result) refinePatch(*result);
    return result;
}

void AgentBridge::feedChunk(std::string_view chunk) {
    streamParser_.feedChunk(chunk);
}

void AgentBridge::onMidiCC(int controller, int value) noexcept {
    // Track CC74 (brightness/filter cutoff) and CC71 (resonance) so the
    // system prompt can reflect the user's current timbral preference.
    switch (controller) {
        case 71: midiResonanceNorm_ = static_cast<float>(value) / 127.0f; break;
        case 74: midiCutoffNorm_    = static_cast<float>(value) / 127.0f; break;
        default: break;
    }
}

// ── Issue #72: Bidirectional knob bridge ─────────────────────────────────────

void AgentBridge::handleKnobTweak(const std::string& param, float value) {
    // Copy current patch, apply the single-parameter delta, inject immediately.
    PatchStruct patch = pipeline_.currentPatch();
    mapper::apply_delta(patch, paramToDelta(param, value));
    pipeline_.injectPatch(patch);
    // Record so the session memory can bias future generations towards user tweaks.
    memory_.recordFeedback(FeedbackKind::Tweak, param, patch);
}

void AgentBridge::handleTextMessage(const std::string& json, int clientId) {
    const std::string type = jsStr(json, "type");

    if (type == "knob_tweak") {
        handleKnobTweak(jsStr(json, "param"), jsFloat(json, "value"));

    } else if (type == "get_dictionary") {
        if (wsb_) wsb_->sendToClient(clientId, getDictionaryJson());

    } else if (type == "save_dictionary") {
        saveDictionary(json);

    } else if (type == "get_telemetry") {
        if (wsb_) wsb_->sendToClient(clientId, getTelemetryJson());

    } else if (type == "set_telemetry_enabled") {
        setTelemetryEnabled(jsBool(json, "enabled"));
        if (wsb_) wsb_->sendToClient(clientId, getTelemetryJson());

    } else if (type == "generate") {
        // Issue #85: submit prompt, then send back narrative rationale.
        const std::string userPrompt = jsStr(json, "prompt");
        const PatchStruct patch = submitPrompt(userPrompt);
        if (wsb_) {
            const std::string rationale = generateRationale(userPrompt, patch);
            // Escape for JSON
            std::string escaped;
            escaped.reserve(rationale.size());
            for (char c : rationale) {
                if (c == '"')       escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n";
                else                escaped += c;
            }
            const std::string msg = "{\"type\":\"rationale\",\"text\":\"" + escaped + "\"}";
            wsb_->sendToClient(clientId, msg);
        }
    }
}

// ── Issue #90: Semantic dictionary ───────────────────────────────────────────

std::string AgentBridge::getDictionaryJson() const {
    return "{\"type\":\"dictionary_data\",\"entries\":" + semanticMapper_.dumpAllToJson() + "}";
}

void AgentBridge::saveDictionary(const std::string& json) {
    semanticMapper_.parseAndSaveCustomEntries(json, "descriptor_dataset_custom.json");
}

void AgentBridge::loadDictionary(const std::string& path) {
    semanticMapper_.loadCustomEntries(path);
}

// ── Issue #91: Telemetry ──────────────────────────────────────────────────────

std::string AgentBridge::getTelemetryJson() const {
    return "{\"type\":\"telemetry_data\"," + telemetry_.toJson().substr(1); // splice type field in
}

void AgentBridge::setTelemetryEnabled(bool on) {
    telemetry_.setEnabled(on);
    if (!on) telemetry_.flush(); // flush remaining records when disabling
}

// ── Issue #85: Session-aware narrative generation ────────────────────────────

std::string AgentBridge::generateRationale(const std::string& prompt,
                                            const PatchStruct& patch) const {
    std::ostringstream oss;

    // Describe oscillator character.
    static const char* kOscNames[] = {
        "sine", "triangle", "sawtooth", "square", "pulse", "wavetable", "FM", "noise"
    };
    const int oscIdx = static_cast<int>(patch.osc[0].type);
    const char* oscName = (oscIdx >= 0 && oscIdx < 8) ? kOscNames[oscIdx] : "sawtooth";

    oss << "I chose a " << oscName << " oscillator";

    // Filter character.
    if (patch.filter.cutoff_hz < 500.0f)
        oss << " with a closed filter for a dark, sub-heavy character";
    else if (patch.filter.cutoff_hz < 4000.0f)
        oss << " with a mid-range filter for warmth and presence";
    else
        oss << " with an open filter for brightness and clarity";

    if (patch.filter.resonance > 0.6f)
        oss << ", pushing the resonance for an acidic edge";
    else if (patch.filter.resonance > 0.3f)
        oss << " with moderate resonance for character";

    // Amplitude envelope.
    if (patch.amp_env.attack_s > 0.5f)
        oss << ". The slow attack lets the sound bloom gradually";
    else if (patch.amp_env.attack_s < 0.01f)
        oss << ". The instant attack gives it punch and immediacy";

    if (patch.amp_env.release_s > 1.5f)
        oss << " with a long release tail";

    // Modulation / movement.
    if (patch.lfo[0].depth > 0.3f) {
        const char* lfoTarget =
            (patch.lfo[0].target == LfoTarget::Pitch)       ? "pitch modulation" :
            (patch.lfo[0].target == LfoTarget::FilterCutoff)? "filter movement"  :
            (patch.lfo[0].target == LfoTarget::Amplitude)   ? "tremolo"          :
                                                               "modulation";
        oss << ", adding " << lfoTarget << " for animation";
    }

    // Space.
    if (patch.reverb.mix > 0.4f)
        oss << ". Heavy reverb places it in a wide, ambient space";
    else if (patch.reverb.mix > 0.15f)
        oss << ". Light reverb adds depth without washing it out";

    if (patch.delay.mix > 0.2f)
        oss << " with delay for rhythmic echo";

    // Session context influence.
    const std::string recap = memory_.buildRecap(prompt, 3);
    if (!recap.empty()) {
        oss << ". Your session feedback steered me toward this timbral direction"
            << " — I've adjusted based on what you've liked and passed on previously";
    }

    // MIDI context.
    if (midiCutoffNorm_ < 0.25f)
        oss << ". I respected your MIDI filter position (currently closed/dark)";
    else if (midiCutoffNorm_ > 0.75f)
        oss << ". I matched your MIDI filter position (currently open/bright)";

    oss << ".";
    return oss.str();
}

} // namespace agentic_synth::agent
