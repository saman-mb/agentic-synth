#include "StyleTransfer.h"

namespace agentic_synth::engine {

StyleTransfer::StyleProfile StyleTransfer::extract(const PatchStruct& ref) {
    StyleProfile profile{};

    // Filter character: map cutoff to 0-1 range
    profile.filterCharacter = 1.0f - std::clamp(ref.filterCutoffHz / 10000.0f, 0.0f, 1.0f);

    // Envelope shape: longer attack + longer decay = more sustained
    float attackNorm = std::clamp(ref.ampAttackMs / 2000.0f, 0.0f, 1.0f);
    float decayNorm = std::clamp(ref.ampDecayMs / 2000.0f, 0.0f, 1.0f);
    float sustainNorm = ref.ampSustainLevel;
    profile.envelopeShape = (attackNorm * 0.3f + decayNorm * 0.3f + sustainNorm * 0.4f);

    // Modulation feel
    profile.modulationFeel = std::clamp(ref.lfoDepth / 1.0f, 0.0f, 1.0f);

    // Brightness based on filter cutoff and oscillator mix
    float cutoffBrightness = std::clamp(ref.filterCutoffHz / 12000.0f, 0.0f, 1.0f);
    float oscBrightness = 0.5f + 0.5f * (1.0f - ref.oscillatorMix[2]);
    profile.brightness = (cutoffBrightness * 0.6f + oscBrightness * 0.4f);

    return profile;
}

PatchStruct StyleTransfer::apply(const PatchStruct& target,
                                  const StyleProfile& style,
                                  float blend) {
    PatchStruct result = target;

    // Blend filter character
    float targetCutoff = result.filterCutoffHz;
    float styledCutoff = 10000.0f * (1.0f - style.filterCharacter);
    result.filterCutoffHz = targetCutoff * (1.0f - blend) + styledCutoff * blend;

    // Blend envelope shape
    // style.envelopeShape 0 = percussive (short A/D, low S), 1 = sustained (long A/D, high S)
    float styledAttack = style.envelopeShape * 1500.0f + 10.0f;
    float styledDecay = style.envelopeShape * 1500.0f + 10.0f;
    float styledSustain = style.envelopeShape;
    float styledRelease = style.envelopeShape * 2000.0f + 10.0f;

    result.ampAttackMs = result.ampAttackMs * (1.0f - blend) + styledAttack * blend;
    result.ampDecayMs = result.ampDecayMs * (1.0f - blend) + styledDecay * blend;
    result.ampSustainLevel = result.ampSustainLevel * (1.0f - blend) + styledSustain * blend;
    result.ampReleaseMs = result.ampReleaseMs * (1.0f - blend) + styledRelease * blend;

    // Blend modulation feel
    result.lfoDepth = result.lfoDepth * (1.0f - blend) + style.modulationFeel * blend;

    // Blend brightness
    if (style.brightness > 0.5f) {
        result.oscillatorMix[0] = 0.6f;  // saw
        result.oscillatorMix[2] = 0.2f;  // less triangle
    } else {
        result.oscillatorMix[0] = 0.2f;  // less saw
        result.oscillatorMix[2] = 0.6f;  // more triangle
    }

    return result;
}

PatchStruct StyleTransfer::transfer(const PatchStruct& reference,
                                     const PatchStruct& target,
                                     float blend) {
    auto profile = extract(reference);
    return apply(target, profile, blend);
}

} // namespace agentic_synth::engine
