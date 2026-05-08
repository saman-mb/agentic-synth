#include "engine/PresetExporter.h"
#include "engine/PatchStruct.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace agentic_synth::engine;
using namespace agentic_synth;

static PatchStruct makeTestPatch() {
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 1200.0f;
    p.filter.resonance = 0.4f;
    p.amp_env.attack_s = 0.01f;
    p.amp_env.decay_s = 0.3f;
    p.amp_env.sustain = 0.8f;
    p.amp_env.release_s = 0.5f;
    p.lfo[0].rate_hz = 2.0f;
    p.lfo[0].depth = 0.3f;
    p.osc[0].volume = 1.0f;
    p.master_gain = 0.9f;
    return p;
}

TEST_CASE("PresetExporter: unmappedParameters returns a list", "[preset-exporter]") {
    PatchStruct patch = makeTestPatch();
    auto unmapped = PresetExporter::unmappedParameters(patch);
    // There may or may not be unmapped params, but the call must not crash
    (void)unmapped;
    SUCCEED();
}

TEST_CASE("PresetExporter: exportSerum writes to temp file", "[preset-exporter]") {
    PresetExporter exporter;
    PatchStruct patch = makeTestPatch();

    std::string tmpPath = std::filesystem::temp_directory_path().string() + "/test_serum.fxp";
    auto report = exporter.exportSerum(patch, tmpPath);

    CHECK_FALSE(report.mappedParams.empty());
    std::filesystem::remove(tmpPath); // cleanup
}

TEST_CASE("PresetExporter: exportVital writes to temp file", "[preset-exporter]") {
    PresetExporter exporter;
    PatchStruct patch = makeTestPatch();

    std::string tmpPath = std::filesystem::temp_directory_path().string() + "/test_vital.vital";
    auto report = exporter.exportVital(patch, tmpPath);

    CHECK_FALSE(report.mappedParams.empty());
    std::filesystem::remove(tmpPath); // cleanup
}
