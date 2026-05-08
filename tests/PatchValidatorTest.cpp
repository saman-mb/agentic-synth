#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>

#include "engine/PatchValidator.h"
#include "mapper/UnsafeMode.h"

using namespace agentic_synth;

// Fill a PatchStruct with random bit patterns, including potential NaN/Inf.
static PatchStruct make_random_patch(std::mt19937& rng) noexcept {
    PatchStruct p{};
    auto* raw = reinterpret_cast<uint8_t*>(&p);
    std::uniform_int_distribution<unsigned> byte_dist(0u, 255u);
    for (std::size_t i = 0; i < sizeof(PatchStruct); ++i)
        raw[i] = static_cast<uint8_t>(byte_dist(rng));
    return p;
}

// ── Finite-field guarantee ────────────────────────────────────────────────────

TEST_CASE("fuzz: 10000 random patches all fields finite after validate_patch") {
    std::mt19937 rng(0xDEAD'BEEFu);
    for (int i = 0; i < 10'000; ++i) {
        PatchStruct raw = make_random_patch(rng);
        PatchStruct validated = validate_patch(raw);
        INFO("iteration " << i);
        REQUIRE(patch_is_finite(validated));
    }
}

// ── Per-field bound checks (10k fuzzes each) ──────────────────────────────────

TEST_CASE("fuzz: filter cutoff within [20, 18000] Hz") {
    std::mt19937 rng(1u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.filter.cutoff_hz >= kFilterCutoffFloor);
        REQUIRE(v.filter.cutoff_hz <= kFilterCutoffCeiling);
    }
}

TEST_CASE("fuzz: resonance within safe ceiling without unsafe mode") {
    std::mt19937 rng(2u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.filter.resonance >= 0.0f);
        REQUIRE(v.filter.resonance <= kSafeResonanceCeiling);
    }
}

TEST_CASE("fuzz: resonance allowed up to 1.0 in unsafe mode") {
    PatchStruct p = make_default_patch();
    p.filter.resonance = 0.95f;
    UnsafeModeFlags unsafe{true};
    auto v = validate_patch(p, unsafe);
    REQUIRE(v.filter.resonance >= 0.0f);
    REQUIRE(v.filter.resonance <= 1.0f);
    // 0.95 is within [0,1] so it must survive unchanged
    REQUIRE(v.filter.resonance == 0.95f);
}

TEST_CASE("fuzz: amp envelope times within published bounds") {
    std::mt19937 rng(3u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.amp_env.attack_s >= 0.0f);
        REQUIRE(v.amp_env.attack_s <= 10.0f);
        REQUIRE(v.amp_env.decay_s >= 0.0f);
        REQUIRE(v.amp_env.decay_s <= 10.0f);
        REQUIRE(v.amp_env.sustain >= 0.0f);
        REQUIRE(v.amp_env.sustain <= 1.0f);
        REQUIRE(v.amp_env.release_s >= 0.0f);
        REQUIRE(v.amp_env.release_s <= 20.0f);
    }
}

TEST_CASE("fuzz: filter envelope times within published bounds") {
    std::mt19937 rng(4u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.filter_env.attack_s >= 0.0f);
        REQUIRE(v.filter_env.attack_s <= 10.0f);
        REQUIRE(v.filter_env.decay_s >= 0.0f);
        REQUIRE(v.filter_env.decay_s <= 10.0f);
        REQUIRE(v.filter_env.sustain >= 0.0f);
        REQUIRE(v.filter_env.sustain <= 1.0f);
        REQUIRE(v.filter_env.release_s >= 0.0f);
        REQUIRE(v.filter_env.release_s <= 20.0f);
    }
}

TEST_CASE("fuzz: LFO rate and depth within bounds") {
    std::mt19937 rng(5u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        for (int j = 0; j < kMaxLfos; ++j) {
            REQUIRE(v.lfo[j].rate_hz >= 0.01f);
            REQUIRE(v.lfo[j].rate_hz <= 20.0f);
            REQUIRE(v.lfo[j].depth >= 0.0f);
            REQUIRE(v.lfo[j].depth <= 1.0f);
        }
    }
}

TEST_CASE("fuzz: oscillator fields within bounds") {
    std::mt19937 rng(6u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        for (int j = 0; j < kMaxOscillators; ++j) {
            REQUIRE(v.osc[j].semitone_offset >= -48.0f);
            REQUIRE(v.osc[j].semitone_offset <= 48.0f);
            REQUIRE(v.osc[j].detune_cents >= -100.0f);
            REQUIRE(v.osc[j].detune_cents <= 100.0f);
            REQUIRE(v.osc[j].volume >= 0.0f);
            REQUIRE(v.osc[j].volume <= 1.0f);
            REQUIRE(v.osc[j].pan >= -1.0f);
            REQUIRE(v.osc[j].pan <= 1.0f);
            REQUIRE(v.osc[j].pulse_width >= 0.01f);
            REQUIRE(v.osc[j].pulse_width <= 0.99f);
            REQUIRE(v.osc[j].fm_ratio >= 0.5f);
            REQUIRE(v.osc[j].fm_ratio <= 16.0f);
            REQUIRE(v.osc[j].fm_depth >= 0.0f);
            REQUIRE(v.osc[j].fm_depth <= 1.0f);
        }
    }
}

TEST_CASE("fuzz: master_gain within [0, 1]") {
    std::mt19937 rng(7u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.master_gain >= 0.0f);
        REQUIRE(v.master_gain <= 1.0f);
    }
}

TEST_CASE("fuzz: voice_count within [1, 16]") {
    std::mt19937 rng(8u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.voice_count >= 1);
        REQUIRE(v.voice_count <= 16);
    }
}

TEST_CASE("fuzz: portamento within [0, 10]") {
    std::mt19937 rng(9u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.portamento_s >= 0.0f);
        REQUIRE(v.portamento_s <= 10.0f);
    }
}

TEST_CASE("fuzz: delay and reverb fields within [0,1] / [0,2]") {
    std::mt19937 rng(10u);
    for (int i = 0; i < 10'000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.reverb.size >= 0.0f);
        REQUIRE(v.reverb.size <= 1.0f);
        REQUIRE(v.reverb.damping >= 0.0f);
        REQUIRE(v.reverb.damping <= 1.0f);
        REQUIRE(v.reverb.width >= 0.0f);
        REQUIRE(v.reverb.width <= 1.0f);
        REQUIRE(v.reverb.mix >= 0.0f);
        REQUIRE(v.reverb.mix <= 1.0f);
        REQUIRE(v.delay.time_s >= 0.0f);
        REQUIRE(v.delay.time_s <= 2.0f);
        REQUIRE(v.delay.feedback >= 0.0f);
        REQUIRE(v.delay.feedback <= 0.99f);
        REQUIRE(v.delay.mix >= 0.0f);
        REQUIRE(v.delay.mix <= 1.0f);
    }
}

// ── patch_is_finite unit tests ────────────────────────────────────────────────

TEST_CASE("patch_is_finite: default patch is finite") { REQUIRE(patch_is_finite(make_default_patch())); }

TEST_CASE("patch_is_finite: NaN cutoff returns false") {
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(patch_is_finite(p));
}

TEST_CASE("patch_is_finite: Inf master_gain returns false") {
    PatchStruct p = make_default_patch();
    p.master_gain = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(patch_is_finite(p));
}

TEST_CASE("patch_is_finite: -Inf reverb size returns false") {
    PatchStruct p = make_default_patch();
    p.reverb.size = -std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(patch_is_finite(p));
}

// ── Determinism ───────────────────────────────────────────────────────────────

TEST_CASE("validate_patch: idempotent — validating a validated patch is a no-op") {
    std::mt19937 rng(42u);
    for (int i = 0; i < 1000; ++i) {
        auto once = validate_patch(make_random_patch(rng));
        auto twice = validate_patch(once);
        REQUIRE(std::memcmp(&once, &twice, sizeof(PatchStruct)) == 0);
    }
}

TEST_CASE("validate_patch: version field always set to kPatchStructVersion") {
    std::mt19937 rng(99u);
    for (int i = 0; i < 1000; ++i) {
        auto v = validate_patch(make_random_patch(rng));
        REQUIRE(v.version == kPatchStructVersion);
    }
}
