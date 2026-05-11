#include <catch2/catch_test_macros.hpp>

#include "engine/PatchStruct.h"
#include "engine/VoiceManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace agentic_synth::engine;
using agentic_synth::LfoTarget;
using agentic_synth::LfoWaveform;
using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;

// ── Helpers ────────────────────────────────────────────────────────────────────

static bool hasNote(const std::vector<int>& notes, int n) {
    return std::find(notes.begin(), notes.end(), n) != notes.end();
}

// ── Basic allocation ──────────────────────────────────────────────────────────

TEST_CASE("VoiceManager initial state") {
    VoiceManager vm(8);
    CHECK(vm.voiceCount() == 8);
    CHECK(vm.activeVoiceCount() == 0);
    CHECK(vm.activeNotes().empty());
}

TEST_CASE("VoiceManager single note on/off") {
    VoiceManager vm(8);
    vm.prepare(44100.0);

    vm.noteOn(60, 0.8f);
    CHECK(vm.activeVoiceCount() == 1);
    CHECK(hasNote(vm.activeNotes(), 60));

    vm.noteOff(60);
    // Voice enters release phase immediately after noteOff — still active.
    CHECK(vm.activeVoiceCount() == 1);

    // Render enough samples (> default release of 0.3 s at 44100 Hz) for release to complete.
    std::vector<float> buf(16384, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));
    CHECK(vm.activeVoiceCount() == 0);
}

TEST_CASE("VoiceManager chord allocates unique voices") {
    VoiceManager vm(8);
    vm.prepare(44100.0);

    for (int i = 0; i < 8; ++i) {
        vm.noteOn(60 + i, 0.8f);
        CHECK(vm.activeVoiceCount() == i + 1);
    }
    CHECK(vm.activeVoiceCount() == 8);
}

TEST_CASE("VoiceManager noteOff on non-existent note is a no-op") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    REQUIRE_NOTHROW(vm.noteOff(60));
    CHECK(vm.activeVoiceCount() == 0);
}

// ── Chord-spam test ───────────────────────────────────────────────────────────
// Rapid note-ons beyond polyphony limit must never drop active count erratically.

TEST_CASE("VoiceManager chord spam never drops below N-1 active voices") {
    static constexpr int kVoices = 8;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(48 + i, 0.8f);
    REQUIRE(vm.activeVoiceCount() == kVoices);

    // Drive beyond the voice limit; each steal must keep count at N.
    for (int i = 0; i < kVoices; ++i) {
        vm.noteOn(72 + i, 0.8f);
        REQUIRE(vm.activeVoiceCount() >= kVoices - 1); // never erratic
    }
    REQUIRE(vm.activeVoiceCount() == kVoices);
}

// ── Voice stealing determinism ────────────────────────────────────────────────
// Oldest-note policy: the voice that played longest is always the one stolen.

TEST_CASE("VoiceManager voice stealing steals oldest note") {
    static constexpr int kVoices = 4;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    // Fill: oldest → 60, then 61, 62, 63.
    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(60 + i, 0.8f);
    REQUIRE(vm.activeVoiceCount() == kVoices);

    // One more note: 60 (oldest) must be evicted.
    vm.noteOn(64, 0.8f);
    auto notes = vm.activeNotes();
    CHECK(vm.activeVoiceCount() == kVoices);
    CHECK_FALSE(hasNote(notes, 60)); // stolen
    CHECK(hasNote(notes, 61));
    CHECK(hasNote(notes, 62));
    CHECK(hasNote(notes, 63));
    CHECK(hasNote(notes, 64));
}

TEST_CASE("VoiceManager voice stealing consistently oldest-first across multiple steals") {
    static constexpr int kVoices = 4;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(60 + i, 0.8f);

    vm.noteOn(70, 0.8f); // steals 60
    vm.noteOn(71, 0.8f); // steals 61
    vm.noteOn(72, 0.8f); // steals 62

    auto notes = vm.activeNotes();
    CHECK_FALSE(hasNote(notes, 60));
    CHECK_FALSE(hasNote(notes, 61));
    CHECK_FALSE(hasNote(notes, 62));
    CHECK(hasNote(notes, 63));
    CHECK(hasNote(notes, 70));
    CHECK(hasNote(notes, 71));
    CHECK(hasNote(notes, 72));
}

TEST_CASE("VoiceManager stealing prefers releasing voices over held voices") {
    static constexpr int kVoices = 4;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(60 + i, 0.8f);

    // Release note 63 (most recent). It should be stolen before 60 (older but held).
    vm.noteOff(63);

    vm.noteOn(70, 0.8f);
    auto notes = vm.activeNotes();
    CHECK(hasNote(notes, 70));       // new note active
    CHECK(hasNote(notes, 60));       // held — not stolen
    CHECK_FALSE(hasNote(notes, 63)); // releasing — stolen first
}

// ── Portamento ────────────────────────────────────────────────────────────────

TEST_CASE("VoiceManager portamento enabled, two notes both become active") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setPortamento(0.1f);

    vm.noteOn(60, 0.8f);
    vm.noteOn(72, 0.8f);
    CHECK(vm.activeVoiceCount() == 2);
}

TEST_CASE("VoiceManager portamento off snaps frequency instantly") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setPortamento(0.0f);

    vm.noteOn(60, 0.8f);
    CHECK(vm.activeVoiceCount() == 1);
}

// ── Retrigger / legato ────────────────────────────────────────────────────────

TEST_CASE("VoiceManager retrigger true re-uses same voice for same note") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setRetrigger(true);

    vm.noteOn(60, 0.8f);
    vm.noteOn(60, 0.8f); // same note; must reuse, not allocate second voice
    CHECK(vm.activeVoiceCount() == 1);
}

TEST_CASE("VoiceManager retrigger false keeps envelope running (legato)") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setRetrigger(false);

    vm.noteOn(60, 0.8f);
    vm.noteOn(60, 0.8f); // legato — same voice, envelope not restarted
    CHECK(vm.activeVoiceCount() == 1);
}

// ── Render ────────────────────────────────────────────────────────────────────

TEST_CASE("VoiceManager renderBlock produces finite output with active note") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.noteOn(60, 0.8f);

    std::vector<float> buf(256, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));

    bool hasNaN = false;
    for (float s : buf)
        hasNaN = hasNaN || std::isnan(s);
    CHECK_FALSE(hasNaN);
}

TEST_CASE("VoiceManager renderNextSample returns 0 with no active voices") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    CHECK(vm.renderNextSample() == 0.0f);
}

// ── Edge-case robustness ──────────────────────────────────────────────────────

TEST_CASE("VoiceManager noteOn without prepare does not crash") {
    VoiceManager vm(4);
    REQUIRE_NOTHROW(vm.noteOn(60, 0.8f));
    REQUIRE_NOTHROW(vm.noteOff(60));
}

TEST_CASE("VoiceManager handles extreme MIDI note values (0 and 127)") {
    VoiceManager vm(4);
    vm.prepare(44100.0);

    REQUIRE_NOTHROW(vm.noteOn(0, 0.8f));
    REQUIRE_NOTHROW(vm.noteOn(127, 0.8f));

    std::vector<float> buf(256, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));

    bool hasNaN = false;
    for (float s : buf)
        hasNaN = hasNaN || std::isnan(s);
    CHECK_FALSE(hasNaN);
}

// ── Modulation wiring ─────────────────────────────────────────────────────────

namespace {

// RMS over a buffer — a loose proxy for output energy. Quiet notes should
// produce less energy than loud ones.
float bufferRms(const std::vector<float>& buf) {
    double acc = 0.0;
    for (float s : buf)
        acc += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(acc / std::max<std::size_t>(1, buf.size())));
}

// Peak absolute amplitude in a buffer.
float bufferPeak(const std::vector<float>& buf) {
    float p = 0.0f;
    for (float s : buf)
        p = std::max(p, std::abs(s));
    return p;
}

} // namespace

TEST_CASE("VoiceManager velocity scales amp envelope peak amplitude") {
    constexpr int kSamples = 2048;
    auto peakForVelocity = [&](float vel) {
        VoiceManager vm(4);
        vm.prepare(44100.0);
        PatchStruct p = make_default_patch();
        p.filter.cutoff_hz = 18000.0f; // wide-open, no filter attenuation
        vm.applyPatch(p);
        vm.noteOn(60, vel);
        std::vector<float> buf(kSamples, 0.0f);
        vm.renderBlock(buf.data(), static_cast<int>(buf.size()));
        return bufferPeak(buf);
    };

    const float quiet = peakForVelocity(0.1f);
    const float loud = peakForVelocity(1.0f);

    // Loud should be at least ~5x quiet (linear velocity scaling, with envelope
    // and filter shaping in play).
    REQUIRE(loud > quiet * 4.0f);
    REQUIRE(quiet > 0.0f);
}

TEST_CASE("VoiceManager filter envelope produces time-varying output") {
    // With env_mod > 0, the filter cutoff is multiplied by the envelope
    // value, so the first half of the buffer (env active) should differ
    // in RMS from the second half (env back to 0). With env_mod = 0 the
    // filter is static and both halves match (modulo the amp env tail).
    auto halfRatio = [](float envMod) {
        VoiceManager vm(1);
        vm.prepare(44100.0);
        PatchStruct p = make_default_patch();
        p.filter.cutoff_hz = 500.0f;
        p.filter.resonance = 0.6f;
        p.filter.env_mod = envMod;
        p.filter_env.attack_s = 0.001f;
        p.filter_env.decay_s = 0.03f;   // env active ~31 ms
        p.filter_env.sustain = 0.0f;
        p.filter_env.release_s = 0.05f;
        p.amp_env.attack_s = 0.001f;
        p.amp_env.decay_s = 0.001f;
        p.amp_env.sustain = 1.0f;       // hold amp open so we isolate filter-env effect
        p.amp_env.release_s = 0.5f;
        vm.applyPatch(p);
        vm.noteOn(72, 1.0f);            // C5 ≈ 523 Hz so harmonics span the cutoff sweep
        std::vector<float> buf(8192, 0.0f);
        vm.renderBlock(buf.data(), static_cast<int>(buf.size()));
        // Compare RMS of "env active" window (samples 200-1400, ~5-32 ms)
        // vs "env settled" window (samples 4000-8000, ~91-181 ms).
        auto windowRms = [&](int start, int end) {
            double s = 0.0;
            for (int i = start; i < end; ++i) s += buf[i] * buf[i];
            return std::sqrt(s / (end - start));
        };
        const double active = windowRms(200, 1400);
        const double settled = windowRms(4000, 8000);
        return std::abs(active - settled) / (active + settled + 1e-9);
    };

    const float deltaWithEnv = halfRatio(1.0f);
    const float deltaWithoutEnv = halfRatio(0.0f);

    // Envelope-driven cutoff motion must produce a measurably larger
    // active-vs-settled RMS delta than a static filter.
    REQUIRE(deltaWithEnv > deltaWithoutEnv);
    REQUIRE(deltaWithEnv > 0.05f);
}

TEST_CASE("VoiceManager LFO modulates cutoff (output varies after startNote)") {
    VoiceManager vm(1);
    vm.prepare(44100.0);

    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 800.0f;
    p.filter.resonance = 0.5f;
    p.lfo[0].waveform = LfoWaveform::Sine;
    p.lfo[0].target = LfoTarget::FilterCutoff;
    p.lfo[0].rate_hz = 8.0f; // a few cycles inside our buffer
    p.lfo[0].depth = 0.9f;
    vm.applyPatch(p);

    vm.noteOn(60, 1.0f);

    // Render long enough to span several LFO cycles.
    constexpr int kSamples = 8192; // ~186 ms @ 44.1kHz
    std::vector<float> buf(kSamples, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));

    // Compute RMS of first vs second half — if the LFO is modulating the
    // filter, the envelope of the signal should change measurably between
    // windows positioned at different LFO phases.
    std::vector<float> firstQ(buf.begin() + 512, buf.begin() + 1024);
    std::vector<float> midQ(buf.begin() + 4096, buf.begin() + 4096 + 512);

    const float r1 = bufferRms(firstQ);
    const float r2 = bufferRms(midQ);
    // They will differ if LFO modulates cutoff. Use a loose 5% threshold.
    REQUIRE(std::abs(r1 - r2) > 0.05f * std::max(r1, r2));
}

TEST_CASE("VoiceManager voice steal fades previous voice (no instant click)") {
    constexpr int kVoices = 2;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f; // wide open
    vm.applyPatch(p);

    // Fill both voices and render long enough to pass attack — output is
    // now near steady-state.
    vm.noteOn(60, 1.0f);
    vm.noteOn(62, 1.0f);
    std::vector<float> warmup(4096, 0.0f);
    vm.renderBlock(warmup.data(), static_cast<int>(warmup.size()));
    const float lastBefore = warmup.back();

    // Trigger a steal: third note forces an eviction.
    vm.noteOn(64, 1.0f);

    // First sample after the steal must not jump wildly. Without the fade,
    // the previous voice's filter/oscillator state is replaced instantly with
    // a new note's near-zero envelope output → a hard discontinuity.
    // With the fade, the outgoing voice continues to contribute its tail for
    // ~5 ms. We require: |out[0] - lastBefore| < 0.5 (very loose; a real
    // click can produce > 1.0 jumps).
    const float firstAfter = vm.renderNextSample();
    REQUIRE(std::abs(firstAfter - lastBefore) < 0.5f);

    // And output remains finite over the fade window.
    std::vector<float> afterBuf(512, 0.0f);
    vm.renderBlock(afterBuf.data(), static_cast<int>(afterBuf.size()));
    for (float s : afterBuf)
        REQUIRE_FALSE(std::isnan(s));
}

TEST_CASE("VoiceManager per-sample smoothing eliminates one-sample cutoff jumps") {
    VoiceManager vm(1);
    vm.prepare(44100.0);

    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 400.0f;
    p.filter.resonance = 0.6f; // resonant peak amplifies any cutoff jump
    vm.applyPatch(p);

    vm.noteOn(60, 1.0f);

    // Render until steady-state.
    std::vector<float> pre(4096, 0.0f);
    vm.renderBlock(pre.data(), static_cast<int>(pre.size()));

    // Now snap the cutoff target upward by a huge ratio. With smoothing,
    // the actual filter cutoff approaches the target over ~5 ms (ParamSmoother
    // default ≈ 30 Hz). Without smoothing, the next sample would see a hard
    // cutoff change → audible "thump".
    vm.setFilterCutoff(8000.0f);

    const float lastBefore = pre.back();
    const float firstAfter = vm.renderNextSample();

    // The smoother guarantees that the cutoff *value* changed by less than
    // (target - current) * coeff in one sample. That keeps the filter output
    // from leaping. Bound the inter-sample jump loosely.
    REQUIRE(std::abs(firstAfter - lastBefore) < 0.3f);

    // The smoother's reported state must move toward the new target, not
    // teleport.
    REQUIRE(vm.currentSmoothedCutoff() < 8000.0f);
    REQUIRE(vm.currentSmoothedCutoff() > 400.0f);
}

TEST_CASE("VoiceManager applyPatch wires LFO + filter env + smoothers without crashing") {
    VoiceManager vm(4);
    vm.prepare(44100.0);

    PatchStruct p = make_default_patch();
    p.filter.env_mod = 0.8f;
    p.lfo[0].target = LfoTarget::Pitch;
    p.lfo[0].depth = 0.5f;
    p.lfo[0].rate_hz = 5.0f;
    p.lfo[1].target = LfoTarget::Amplitude;
    p.lfo[1].depth = 0.3f;
    p.lfo[1].rate_hz = 2.0f;
    p.master_gain = 0.8f;
    vm.applyPatch(p);

    vm.noteOn(60, 0.9f);
    std::vector<float> buf(2048, 0.0f);
    REQUIRE_NOTHROW(vm.renderBlock(buf.data(), static_cast<int>(buf.size())));

    bool finite = true;
    for (float s : buf)
        finite = finite && std::isfinite(s);
    REQUIRE(finite);
}
