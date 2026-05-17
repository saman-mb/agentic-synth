// Phase D / #268 (partial) — OfflineRenderer unit coverage.
//
// Asserts the bounce path produces audible content of the requested size,
// and that two perceptually-different patches actually render differently.

#include "engine/OfflineRenderer.h"
#include "engine/PatchStruct.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using agentic_synth::engine::BounceConfig;
using agentic_synth::engine::renderPatchToBuffer;
using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;

namespace {

double rms(const std::vector<float>& buf) {
    if (buf.empty())
        return 0.0;
    double sumSq = 0.0;
    for (auto v : buf)
        sumSq += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(sumSq / static_cast<double>(buf.size()));
}

double rmsDifference(const std::vector<float>& a, const std::vector<float>& b) {
    const auto n = std::min(a.size(), b.size());
    if (n == 0)
        return 0.0;
    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    return std::sqrt(sumSq / static_cast<double>(n));
}

PatchStruct brightPatch() {
    auto p = make_default_patch();
    p.filter.cutoff_hz = 14000.0f;
    p.filter.resonance = 0.2f;
    p.osc[0].volume = 0.9f;
    return p;
}

PatchStruct darkPatch() {
    auto p = make_default_patch();
    p.filter.cutoff_hz = 320.0f;
    p.filter.resonance = 0.3f;
    p.osc[0].volume = 0.9f;
    return p;
}

} // namespace

TEST_CASE("OfflineRenderer: renderPatchToBuffer returns non-empty buffer", "[OfflineRenderer]") {
    BounceConfig cfg;
    cfg.duration_s = 1.0f;
    cfg.note_hold_s = 0.6f;
    const auto buf = renderPatchToBuffer(brightPatch(), cfg);
    CHECK_FALSE(buf.empty());
}

TEST_CASE("OfflineRenderer: buffer size matches duration × sample_rate × channels", "[OfflineRenderer]") {
    BounceConfig cfg;
    cfg.duration_s = 2.0f;
    cfg.note_hold_s = 1.5f;
    cfg.sample_rate_hz = 48000;
    cfg.channels = 2;
    const auto buf = renderPatchToBuffer(brightPatch(), cfg);
    const size_t expected = static_cast<size_t>(cfg.duration_s * cfg.sample_rate_hz) * static_cast<size_t>(cfg.channels);
    CHECK(buf.size() == expected);
}

TEST_CASE("OfflineRenderer: produces non-zero audio (RMS > 0.001)", "[OfflineRenderer]") {
    BounceConfig cfg;
    cfg.duration_s = 1.0f;
    cfg.note_hold_s = 0.6f;
    const auto buf = renderPatchToBuffer(brightPatch(), cfg);
    const double r = rms(buf);
    CHECK(r > 0.001);
}

TEST_CASE("OfflineRenderer: two different patches produce different audio", "[OfflineRenderer]") {
    BounceConfig cfg;
    cfg.duration_s = 1.0f;
    cfg.note_hold_s = 0.6f;
    const auto bufBright = renderPatchToBuffer(brightPatch(), cfg);
    const auto bufDark = renderPatchToBuffer(darkPatch(), cfg);
    REQUIRE(bufBright.size() == bufDark.size());
    const double diff = rmsDifference(bufBright, bufDark);
    CHECK(diff > 0.001);
}
