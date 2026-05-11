#pragma once

#include "engine/PatchStruct.h"

#include <string>
#include <vector>

namespace agentic_synth::engine {

// Export patches to Serum .fxp and Vital .vital formats
// where direct parameter mappings exist.
// Parameters without equivalents are documented in the migration report.

class PresetExporter {
public:
    struct MigrationReport {
        std::vector<std::string> mappedParams;
        std::vector<std::string> unmappedParams;
        std::string notes;
    };

    // Export to Serum .fxp format (VST preset)
    // Parameter mapping:
    //   oscillatorMix → oscillator level (map closest type)
    //   filterCutoffHz → filter cutoff
    //   filterResonance → filter resonance
    //   ampAttackMs → amp envelope attack
    //   ampDecayMs → amp envelope decay
    //   ampSustainLevel → amp envelope sustain
    //   ampReleaseMs → amp envelope release
    //   lfoRateHz → modulation rate
    //   lfoDepth → modulation amount
    MigrationReport exportSerum(const PatchStruct& patch, const std::string& path);

    // Export to Vital .vital format
    // Similar parameter mapping structure
    MigrationReport exportVital(const PatchStruct& patch, const std::string& path);

    // List of unmapped parameters for the current export
    static std::vector<std::string> unmappedParameters(const PatchStruct& patch);

private:
    // Serum .fxp header: 4-byte chunk magic + 4-byte size
    static constexpr uint32_t kFxpMagic = 0x4B736466; // "Ksfd" in LE
    static constexpr int kFxpVersion = 1;

    std::vector<uint8_t> buildSerumChunk(const PatchStruct& patch);
    std::vector<uint8_t> buildVitalChunk(const PatchStruct& patch);
};

} // namespace agentic_synth::engine
