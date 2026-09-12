#include <catch2/catch_test_macros.hpp>

#include "agsynth.h"
#include "engine/PatchStruct.h"

#include <cmath>
#include <cstring>
#include <vector>

#if defined(__linux__)
#include <malloc.h>
#endif

using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;

namespace {

PatchStruct default_blob() { return make_default_patch(); }

ags_event note_on(uint8_t note, uint8_t vel, uint32_t offset) {
    ags_event e{};
    e.kind = AGS_EVENT_NOTE_ON;
    e.note = note;
    e.velocity = vel;
    e.sample_offset = offset;
    return e;
}

double rms(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v)
        s += static_cast<double>(x) * x;
    return std::sqrt(s / static_cast<double>(v.size()));
}

} // namespace

TEST_CASE("C API create rejects bad sample rate / block") {
    REQUIRE(ags_engine_create(0.0, 512) == nullptr);
    REQUIRE(ags_engine_create(44100.0, 0) == nullptr);
    REQUIRE(ags_engine_create(44100.0, 99999) == nullptr);
}

TEST_CASE("C API default saw patch renders audible energy") {
    auto* e = ags_engine_create(44100.0, 512);
    REQUIRE(e != nullptr);
    const auto patch = default_blob();
    REQUIRE(ags_engine_set_patch(e, &patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);

    const ags_event on = note_on(60, 100, 0);
    REQUIRE(ags_engine_push_events(e, &on, 1) == AGS_OK);

    std::vector<float> buf(512 * 2, 0.0f);
    REQUIRE(ags_engine_render(e, buf.data(), 512, 2) == AGS_OK);
    REQUIRE(rms(buf) > 0.001);

    ags_engine_destroy(e);
}

TEST_CASE("C API two offline renders are bit-identical") {
    const auto patch = default_blob();
    const ags_event on = note_on(64, 100, 0);
    constexpr uint32_t frames = 2048;
    std::vector<float> a(frames * 2, 0.0f);
    std::vector<float> b(frames * 2, 0.0f);
    REQUIRE(ags_render_offline(&patch, static_cast<uint32_t>(sizeof(patch)), &on, 1, 44100.0, frames, a.data()) ==
            AGS_OK);
    REQUIRE(ags_render_offline(&patch, static_cast<uint32_t>(sizeof(patch)), &on, 1, 44100.0, frames, b.data()) ==
            AGS_OK);
    REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    REQUIRE(rms(a) > 0.001);
}

TEST_CASE("C API malformed patch bytes return errors without crashing") {
    auto* e = ags_engine_create(44100.0, 256);
    REQUIRE(e != nullptr);

    REQUIRE(ags_engine_set_patch(e, nullptr, 0) == AGS_ERR_NULL);
    char tiny[3]{};
    REQUIRE(ags_engine_set_patch(e, tiny, 3) == AGS_ERR_SIZE);

    auto bad_ver = default_blob();
    bad_ver.version = 99;
    REQUIRE(ags_engine_set_patch(e, &bad_ver, static_cast<uint32_t>(sizeof(bad_ver))) == AGS_ERR_PARAM);

    auto nan_patch = default_blob();
    nan_patch.filter.cutoff_hz = std::nanf("");
    REQUIRE(ags_engine_set_patch(e, &nan_patch, static_cast<uint32_t>(sizeof(nan_patch))) == AGS_ERR_PARAM);

    REQUIRE(ags_engine_set_param(e, "not.a.path", 1.0f) == AGS_ERR_PARAM);
    REQUIRE(ags_engine_set_param(e, "filter.cutoff_hz", std::nanf("")) == AGS_ERR_PARAM);

    ags_engine_destroy(e);
}

TEST_CASE("C API param get/set round-trip") {
    auto* e = ags_engine_create(48000.0, 256);
    REQUIRE(e != nullptr);
    REQUIRE(ags_engine_set_param(e, "filter.cutoff_hz", 1234.0f) == AGS_OK);
    float out = 0.0f;
    REQUIRE(ags_engine_get_param(e, "filter.cutoff_hz", &out) == AGS_OK);
    REQUIRE(out == 1234.0f);
    REQUIRE(ags_engine_set_param(e, "osc.0.volume", 0.5f) == AGS_OK);
    REQUIRE(ags_engine_get_param(e, "osc.0.volume", &out) == AGS_OK);
    REQUIRE(out == 0.5f);
    ags_engine_destroy(e);
}

TEST_CASE("C API render does not grow heap (linux mallinfo)") {
#if defined(__linux__)
    auto* e = ags_engine_create(44100.0, 512);
    REQUIRE(e != nullptr);
    const auto patch = default_blob();
    REQUIRE(ags_engine_set_patch(e, &patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);
    const ags_event on = note_on(60, 100, 0);
    REQUIRE(ags_engine_push_events(e, &on, 1) == AGS_OK);

    // Warm the render path so first-call lazy init is not measured.
    std::vector<float> buf(512 * 2, 0.0f);
    REQUIRE(ags_engine_render(e, buf.data(), 512, 2) == AGS_OK);
    REQUIRE(ags_engine_push_events(e, &on, 1) == AGS_OK);

    struct mallinfo2 before = mallinfo2();
    REQUIRE(ags_engine_render(e, buf.data(), 512, 2) == AGS_OK);
    struct mallinfo2 after = mallinfo2();
    REQUIRE(after.uordblks == before.uordblks);

    ags_engine_destroy(e);
#else
    SUCCEED("heap-growth assert is linux mallinfo2 only");
#endif
}

TEST_CASE("C API patch struct size matches C++ POD") {
    REQUIRE(ags_patch_struct_size() == static_cast<uint32_t>(sizeof(PatchStruct)));
}

TEST_CASE("C API header is C linkage size for events") { REQUIRE(sizeof(ags_event) == 12); }
