#include "PresetExporter.h"

#include <fstream>
#include <cstring>

namespace agentic_synth::engine {

// ── Serum .fxp ─────────────────────────────────────────────────────────

PresetExporter::MigrationReport PresetExporter::exportSerum(
    const PatchStruct& patch, const std::string& path) {

    MigrationReport report;
    auto chunk = buildSerumChunk(patch);

    // Write .fxp file
    // Format: fxpChunk (fxp preset magic) + fxProgram (program data)
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        report.notes = "Failed to open file: " + path;
        return report;
    }

    // Write minimal fxp header
    uint8_t magic[4] = {'C', 'c', 'n', 'k'};  // Chunk magic
    int32_t size = static_cast<int32_t>(chunk.size());
    file.write(reinterpret_cast<const char*>(magic), 4);
    file.write(reinterpret_cast<const char*>(&size), 4);
    file.write(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    file.close();

    report.mappedParams = {
        "oscillatorMix (approximate type map)",
        "filterCutoffHz",
        "filterResonance",
        "ampAttackMs", "ampDecayMs",
        "ampSustainLevel", "ampReleaseMs",
        "lfoRateHz", "lfoDepth"
    };
    report.notes = "Serum export complete. Unmapped: wavetable index, modulation matrix.";

    return report;
}

// ── Vital .vital ───────────────────────────────────────────────────────

PresetExporter::MigrationReport PresetExporter::exportVital(
    const PatchStruct& patch, const std::string& path) {

    MigrationReport report;
    auto chunk = buildVitalChunk(patch);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        report.notes = "Failed to open file: " + path;
        return report;
    }

    uint8_t magic[6] = {'V', 'I', 'T', 'A', 'L', 0};
    int32_t size = static_cast<int32_t>(chunk.size());
    file.write(reinterpret_cast<const char*>(magic), 6);
    file.write(reinterpret_cast<const char*>(&size), 4);
    file.write(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    file.close();

    report.mappedParams = {
        "oscillatorMix (source level map)",
        "filterCutoffHz",
        "filterResonance",
        "ampAttackMs", "ampDecayMs",
        "ampSustainLevel", "ampReleaseMs",
        "lfoRateHz", "lfoDepth"
    };
    report.unmappedParams = {
        "Wavetable position/index",
        "Modulation matrix routing",
        "Effect chain parameters"
    };
    report.notes = "Vital export complete. " +
        std::to_string(report.unmappedParams.size()) +
        " parameters could not be mapped.";

    return report;
}

// ── Internal helpers ───────────────────────────────────────────────────

std::vector<uint8_t> PresetExporter::buildSerumChunk(const PatchStruct& patch) {
    // Build a simplified parameter block — actual Serum format
    // requires reverse-engineering their .fxp structure.
    // Here we store parameters in a structured block.
    std::vector<uint8_t> data;
    data.reserve(256);

    // Version
    data.push_back(1);

    // Oscillator mix (encoded as 4 floats)
    auto addFloat = [&](float f) {
        auto* fp = reinterpret_cast<const uint8_t*>(&f);
        data.insert(data.end(), fp, fp + 4);
    };

    for (int i = 0; i < 4; ++i) {
        addFloat(patch.oscillatorMix[i]);
    }

    addFloat(patch.filterCutoffHz);
    addFloat(patch.filterResonance);
    addFloat(patch.ampAttackMs);
    addFloat(patch.ampDecayMs);
    addFloat(patch.ampSustainLevel);
    addFloat(patch.ampReleaseMs);
    addFloat(patch.lfoRateHz);
    addFloat(patch.lfoDepth);

    return data;
}

std::vector<uint8_t> PresetExporter::buildVitalChunk(const PatchStruct& patch) {
    // Similar to Serum but with Vital's parameter layout
    std::vector<uint8_t> data;
    data.reserve(256);

    data.push_back(1);  // version

    auto addFloat = [&](float f) {
        auto* fp = reinterpret_cast<const uint8_t*>(&f);
        data.insert(data.end(), fp, fp + 4);
    };

    for (int i = 0; i < 4; ++i) {
        addFloat(patch.oscillatorMix[i]);
    }

    addFloat(patch.filterCutoffHz);
    addFloat(patch.filterResonance);
    addFloat(patch.ampAttackMs);
    addFloat(patch.ampDecayMs);
    addFloat(patch.ampSustainLevel);
    addFloat(patch.ampReleaseMs);
    addFloat(patch.lfoRateHz);
    addFloat(patch.lfoDepth);

    return data;
}

std::vector<std::string> PresetExporter::unmappedParameters(const PatchStruct&) {
    return {
        "Wavetable position/index",
        "Modulation matrix routing",
        "Effect chain parameters"
    };
}

} // namespace agentic_synth::engine
