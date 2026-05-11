#pragma once

#include <array>
#include <cstdint>

#include "engine/PatchStruct.h"

namespace agentic_synth::engine {

// Generative variation engine for Issue #69.
// Produces 5 audibly distinct PatchStruct variants from a base patch
// via three complementary strategies.
class VariationEngine {
public:
    static constexpr int kVariationCount = 5;

    enum class Strategy {
        TemperatureSweep,
        Perturbation,
        Morph,
    };

    // Temperature sweep: interpolate from base toward a "hot" (maximally
    // contrasting) version at temperatures [0.2, 0.4, 0.6, 0.8, 1.0].
    [[nodiscard]] std::array<PatchStruct, kVariationCount> temperatureSweep(const PatchStruct& base) const noexcept;

    // Parameter perturbation: apply ±15% random variation on correlated
    // parameter groups (brightness, space, movement, character).
    // Each variation uses a different seed for independent directions.
    [[nodiscard]] std::array<PatchStruct, kVariationCount> perturbation(const PatchStruct& base,
                                                                        uint32_t seed = 42) const noexcept;

    // Morph interpolation: linearly interpolate between base and target
    // at positions [0.2, 0.4, 0.6, 0.8, 1.0].
    [[nodiscard]] std::array<PatchStruct, kVariationCount> morph(const PatchStruct& base,
                                                                 const PatchStruct& target) const noexcept;

    // Generate 5 variations concurrently using all three strategies.
    // Draws 2 from temperature, 2 from perturbation, 1 from morph.
    [[nodiscard]] std::array<PatchStruct, kVariationCount> generateVariations(const PatchStruct& base) const;

    // Same as generateVariations but uses a caller-supplied perturbation seed
    // for reproducible variation sets (useful for session replay and A/B testing).
    [[nodiscard]] std::array<PatchStruct, kVariationCount> generateVariationsWithSeed(const PatchStruct& base,
                                                                                      uint32_t perturbSeed) const;
};

} // namespace agentic_synth::engine
