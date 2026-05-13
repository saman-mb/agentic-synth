#include "agent/PromptHandler.h"

#include <iostream>
#include <sstream>

#include "agent/KnobBridge.h"

namespace agentic_synth::agent {

PatchStruct PromptHandler::submitPrompt(const std::string& prompt) {
    // Reset stream parser so field-complete callbacks from a prior LLM call
    // don't bleed into this submission's heuristic patch.
    streamParser_.reset();
    // Issue #65/#68: heuristic dispatched < 200 ms; semantic layer refines in place.
    PatchStruct patch = pipeline_.submit(prompt);
    if (semanticMapper_.apply(prompt, patch) > 0)
        pipeline_.injectPatch(patch);
    return patch;
}

void PromptHandler::refinePatch(const PatchStruct& llmPatch) { pipeline_.refinePatch(llmPatch); }

std::optional<PatchStruct> PromptHandler::generateLlmPatch(const std::string& prompt, uint32_t patch_id) {
    // Try the local llama.cpp /completion server first; on failure (server
    // not running, connect refused, malformed response) fall back to the
    // Gemini API when a GEMINI_KEY was discovered at startup. Both paths
    // share GrammarSampler::parse_patch_json + validate_patch so the
    // resulting PatchStruct is uniformly safe to refine into the pipeline.
    auto result = sampler_.generate(prompt, patch_id);
    if (result) {
        std::cerr << "[PromptHandler] LLM path=local-llama.cpp ok\n";
        refinePatch(*result);
        return result;
    }

    if (gemini_.enabled()) {
        std::cerr << "[PromptHandler] local llama.cpp unavailable; trying Gemini fallback\n";
        result = gemini_.generate(prompt, patch_id);
        if (result) {
            std::cerr << "[PromptHandler] LLM path=gemini ok\n";
            refinePatch(*result);
            return result;
        }
        std::cerr << "[PromptHandler] LLM path=gemini failed; no patch produced\n";
    } else {
        std::cerr << "[PromptHandler] local llama.cpp unavailable and GEMINI_KEY unset; no patch produced\n";
    }
    return std::nullopt;
}

void PromptHandler::feedChunk(std::string_view chunk) { streamParser_.feedChunk(chunk); }

std::string PromptHandler::buildSystemPrompt(const std::string& userPrompt) const {
    const std::string& base =
        sampler_.systemPrompt().empty()
            ? std::string("You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n")
            : sampler_.systemPrompt();

    std::string prompt = base;

    // Append MIDI CC context so the AI respects the user's current performance state.
    const float cutoff = knob_.midiCutoffNorm();
    const float reso = knob_.midiResonanceNorm();
    if (cutoff < 0.25f)
        prompt += "MIDI context: filter is currently closed (dark sound).\n";
    else if (cutoff > 0.75f)
        prompt += "MIDI context: filter is currently open (bright sound).\n";
    if (reso > 0.5f)
        prompt += "MIDI context: high resonance is active.\n";

    std::string recap = memory_.buildRecap(userPrompt);
    if (recap.empty())
        return prompt;
    return prompt + "\n## Session Feedback\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
}

PatchVector PromptHandler::getParameterBias(const std::string& userPrompt) const {
    return memory_.computeParameterBias(userPrompt);
}

std::string PromptHandler::generateRationale(const std::string& prompt, const PatchStruct& patch) const {
    std::ostringstream oss;

    // Describe oscillator character.
    static const char* kOscNames[] = {"sine", "triangle", "sawtooth", "square", "pulse", "wavetable", "FM", "noise"};
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
        const char* lfoTarget = (patch.lfo[0].target == LfoTarget::Pitch)          ? "pitch modulation"
                                : (patch.lfo[0].target == LfoTarget::FilterCutoff) ? "filter movement"
                                : (patch.lfo[0].target == LfoTarget::Amplitude)    ? "tremolo"
                                                                                   : "modulation";
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
    const float cutoff = knob_.midiCutoffNorm();
    if (cutoff < 0.25f)
        oss << ". I respected your MIDI filter position (currently closed/dark)";
    else if (cutoff > 0.75f)
        oss << ". I matched your MIDI filter position (currently open/bright)";

    oss << ".";
    return oss.str();
}

} // namespace agentic_synth::agent
