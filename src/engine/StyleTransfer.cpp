#include "StyleTransfer.h"

#include <algorithm>

namespace agentic_synth::engine {

StyleTransfer::StyleProfile StyleTransfer::extract(const PatchStruct& ref) {
    StyleProfile profile{};

    // Filter character: map cutoff to 0-1 range (0 = open, 1 = closed)
    profile.filterCharacter = 1.0f - std::clamp(ref.filter.cutoff_hz / 10000.0f, 0.0f, 1.0f);

    // Envelope shape: longer attack + longer decay = more sustained
    float attackNorm = std::clamp(ref.amp_env.attack_s / 2.0f, 0.0f, 1.0f);
    float decayNorm = std::clamp(ref.amp_env.decay_s / 2.0f, 0.0f, 1.0f);
    float sustainNorm = ref.amp_env.sustain;
    profile.envelopeShape = (attackNorm * 0.3f + decayNorm * 0.3f + sustainNorm * 0.4f);

    // Modulation feel
    profile.modulationFeel = std::clamp(ref.lfo[0].depth / 1.0f, 0.0f, 1.0f);

    // Brightness based on filter cutoff and oscillator mix
    float cutoffBrightness = std::clamp(ref.filter.cutoff_hz / 12000.0f, 0.0f, 1.0f);
    float oscBrightness = 0.5f + 0.5f * (1.0f - ref.osc[2].volume);
    profile.brightness = (cutoffBrightness * 0.6f + oscBrightness * 0.4f);

    return profile;
}

PatchStruct StyleTransfer::apply(const PatchStruct& target, const StyleProfile& style, float blend) {
    PatchStruct result = target;

    // Blend filter character
    float targetCutoff = result.filter.cutoff_hz;
    float styledCutoff = 10000.0f * (1.0f - style.filterCharacter);
    result.filter.cutoff_hz = targetCutoff * (1.0f - blend) + styledCutoff * blend;

    // style.envelopeShape 0 = percussive (short A/D, low S), 1 = sustained (long A/D, high S)
    float styledAttack = style.envelopeShape * 1.5f + 0.01f;
    float styledDecay = style.envelopeShape * 1.5f + 0.01f;
    float styledSustain = style.envelopeShape;
    float styledRelease = style.envelopeShape * 2.0f + 0.01f;

    result.amp_env.attack_s = result.amp_env.attack_s * (1.0f - blend) + styledAttack * blend;
    result.amp_env.decay_s = result.amp_env.decay_s * (1.0f - blend) + styledDecay * blend;
    result.amp_env.sustain = result.amp_env.sustain * (1.0f - blend) + styledSustain * blend;
    result.amp_env.release_s = result.amp_env.release_s * (1.0f - blend) + styledRelease * blend;

    // Blend modulation feel
    result.lfo[0].depth = result.lfo[0].depth * (1.0f - blend) + style.modulationFeel * blend;

    // Blend brightness via oscillator volumes
    if (style.brightness > 0.5f) {
        result.osc[0].volume = 0.6f; // saw
        result.osc[2].volume = 0.2f; // less triangle
    } else {
        result.osc[0].volume = 0.2f; // less saw
        result.osc[2].volume = 0.6f; // more triangle
    }

    return result;
}

PatchStruct StyleTransfer::transfer(const PatchStruct& reference, const PatchStruct& target, float blend) {
    auto profile = extract(reference);
    return apply(target, profile, blend);
}

} // namespace agentic_synth::engine
