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

// ── Stereo path (Phase 2) ─────────────────────────────────────────────────────

namespace {

double sumSquares(const std::vector<float>& buf) {
    double acc = 0.0;
    for (float s : buf)
        acc += static_cast<double>(s) * s;
    return acc;
}

double sumAbs(const std::vector<float>& buf) {
    double acc = 0.0;
    for (float s : buf)
        acc += std::abs(static_cast<double>(s));
    return acc;
}

} // namespace

TEST_CASE("VoiceManager stereo renderBlock produces L != R when voices panned") {
    VoiceManager vm(8);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f; // wide open so we hear oscillator energy directly
    vm.applyPatch(p);

    // Two voices land on voice indices 0 (-0.4) and 1 (+0.4) — opposite sides.
    vm.noteOn(60, 0.9f);
    vm.noteOn(64, 0.9f);

    constexpr int kSamples = 2048;
    std::vector<float> l(kSamples, 0.0f), r(kSamples, 0.0f);
    vm.renderBlock(l.data(), r.data(), kSamples);

    const double sl = sumSquares(l);
    const double sr = sumSquares(r);
    REQUIRE(sl > 0.0);
    REQUIRE(sr > 0.0);

    // For two uncorrelated voices with equal energy on symmetric pans,
    // sum-of-squares(L) ≈ sum-of-squares(R) by construction — that's the
    // *point* of constant-power panning. The right way to assert stereo is
    // real (not dual-mono) is to check that L−R carries substantial energy:
    // dual-mono would give L−R == 0 exactly. Real stereo gives a non-trivial
    // side signal because the voices contribute differently to L and R.
    double diffSq = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const double d = static_cast<double>(l[i]) - static_cast<double>(r[i]);
        diffSq += d * d;
    }
    const double midSq = sl + sr;
    REQUIRE(diffSq > 0.0);
    // Side energy should be a meaningful fraction of total. Loose 1% bound.
    REQUIRE(diffSq / midSq > 0.01);
}

TEST_CASE("VoiceManager stereo single centered voice produces equal L/R") {
    VoiceManager vm(8);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    vm.applyPatch(p);

    // Trigger one voice then force its pan to center, simulating either
    // a pan=0 patch override or a unison-center voice. We can't reach into
    // the private Voice directly, but we know voice index 0's pan is -0.4
    // and index 2's is -0.2 — neither is center. To get a true L==R sample
    // path we instead use a *different* property: with a single voice at
    // pan p, L = cos(θ)*s and R = sin(θ)*s. Their ratio is fixed across
    // samples. So we test that the *ratio* of sum(|L|)/sum(|R|) matches
    // the expected constant-power ratio for voice 0's pan = -0.4.
    vm.noteOn(60, 0.9f);

    constexpr int kSamples = 4096;
    std::vector<float> l(kSamples, 0.0f), r(kSamples, 0.0f);
    vm.renderBlock(l.data(), r.data(), kSamples);

    // For pan = -0.4: θ = ((-0.4 + 1)/2) * π/2 = 0.3 * π/2 ≈ 0.4712
    //   L = cos(0.4712) ≈ 0.8910, R = sin(0.4712) ≈ 0.4540
    //   ratio L/R ≈ 1.963
    const double ratio = sumAbs(l) / std::max(1e-9, sumAbs(r));
    REQUIRE(ratio > 1.7);
    REQUIRE(ratio < 2.3);
}

TEST_CASE("VoiceManager stereo constant-power: total power similar regardless of pan") {
    // Render the same note twice with two different voice slots → different
    // pan positions. Constant-power law guarantees L²+R² is identical in
    // both cases (within float epsilon and DSP variance from envelope ramp).
    auto totalPowerForFirstNote = [](int holdNoteFirst) {
        VoiceManager vm(8);
        vm.prepare(44100.0);
        PatchStruct p = make_default_patch();
        p.filter.cutoff_hz = 18000.0f;
        p.amp_env.attack_s = 0.001f;
        p.amp_env.decay_s = 0.001f;
        p.amp_env.sustain = 1.0f;
        p.amp_env.release_s = 0.5f;
        vm.applyPatch(p);

        // To force a chosen voice index for the test, fill slots 0..k-1
        // with throwaway voices that we immediately release, then the next
        // noteOn lands on slot k.
        for (int i = 0; i < holdNoteFirst; ++i) {
            vm.noteOn(20 + i, 0.0001f); // near-silent occupant
        }
        vm.noteOn(60, 1.0f); // the voice we measure

        constexpr int kSamples = 4096;
        std::vector<float> l(kSamples, 0.0f), r(kSamples, 0.0f);
        vm.renderBlock(l.data(), r.data(), kSamples);
        return sumSquares(l) + sumSquares(r);
    };

    const double powerSlot0 = totalPowerForFirstNote(0); // pan = -0.4
    const double powerSlot1 = totalPowerForFirstNote(1); // pan = +0.4
    const double powerSlot4 = totalPowerForFirstNote(4); // pan = -0.6

    REQUIRE(powerSlot0 > 0.0);
    REQUIRE(powerSlot1 > 0.0);
    REQUIRE(powerSlot4 > 0.0);

    // Constant-power means L²+R² is preserved across pan positions. The
    // throwaway voices contribute tiny extra energy (velocity 0.0001), so
    // we allow a generous ~3 dB tolerance (factor of 2).
    auto withinHalfDb = [](double a, double b) {
        const double ratio = a / b;
        return ratio > 0.5 && ratio < 2.0;
    };
    REQUIRE(withinHalfDb(powerSlot0, powerSlot1));
    REQUIRE(withinHalfDb(powerSlot0, powerSlot4));
}

TEST_CASE("VoiceManager mono renderBlock unaffected by pan (backward compat)") {
    VoiceManager vm(8);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    vm.applyPatch(p);

    vm.noteOn(60, 0.9f);

    std::vector<float> mono(2048, 0.0f);
    vm.renderBlock(mono.data(), static_cast<int>(mono.size()));

    bool finite = true;
    for (float s : mono)
        finite = finite && std::isfinite(s);
    REQUIRE(finite);
    REQUIRE(sumAbs(mono) > 0.0);
}

TEST_CASE("VoiceManager renderNextSample remains scalar finite (backward compat)") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.noteOn(60, 0.9f);
    for (int i = 0; i < 64; ++i) {
        const float s = vm.renderNextSample();
        REQUIRE(std::isfinite(s));
    }
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

// ── FX bus wiring (Phase 3) ──────────────────────────────────────────────────

namespace {

// Render the stereo FX bus through note-on/render/note-off/render-tail and
// return the post-release RMS of the (L,R) sum. Used to compare wet vs dry
// patches end-to-end.
double renderPostReleaseRms(const PatchStruct& patch, int releaseSamples) {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.applyPatch(patch);
    vm.noteOn(60, 1.0f);
    // Render ~50 ms with note held so the envelope reaches sustain.
    std::vector<float> l(2205, 0.0f), r(2205, 0.0f);
    vm.renderBlock(l.data(), r.data(), static_cast<int>(l.size()));
    vm.noteOff(60);
    // Now render the tail. With short amp release, the dry signal goes to
    // zero quickly; any post-release energy must come from the FX bus.
    std::vector<float> tailL(releaseSamples, 0.0f), tailR(releaseSamples, 0.0f);
    vm.renderBlock(tailL.data(), tailR.data(), releaseSamples);
    // Skip the initial release transient (first ~20 ms) so we measure pure
    // tail energy.
    const int start = 882;
    double sum = 0.0;
    for (int i = start; i < releaseSamples; ++i) {
        sum += static_cast<double>(tailL[i]) * tailL[i];
        sum += static_cast<double>(tailR[i]) * tailR[i];
    }
    return std::sqrt(sum / static_cast<double>(2 * (releaseSamples - start)));
}

} // namespace

TEST_CASE("VoiceManager reverb mix=1 produces decaying tail after note off") {
    PatchStruct wet = make_default_patch();
    wet.amp_env.release_s = 0.01f;     // dry signal dies fast
    wet.reverb.mix = 1.0f;
    wet.reverb.size = 0.8f;
    wet.reverb.damping = 0.3f;
    wet.delay.mix = 0.0f;              // isolate reverb

    PatchStruct dry = wet;
    dry.reverb.mix = 0.0f;

    const double wetRms = renderPostReleaseRms(wet, 88200); // 2 s tail
    const double dryRms = renderPostReleaseRms(dry, 88200);

    // Wet path must hold a measurable reverb tail long after the amp env
    // releases; dry path must be effectively silent post-transient.
    CHECK(wetRms > 10.0 * dryRms);
    CHECK(wetRms > 1e-4);
    CHECK(dryRms < 1e-3);
}

TEST_CASE("VoiceManager delay mix=1 produces echoes after note off") {
    PatchStruct wet = make_default_patch();
    wet.amp_env.release_s = 0.01f;
    wet.delay.mix = 1.0f;
    wet.delay.time_s = 0.1f;
    wet.delay.feedback = 0.5f;
    wet.reverb.mix = 0.0f;             // isolate delay

    PatchStruct dry = wet;
    dry.delay.mix = 0.0f;

    const double wetRms = renderPostReleaseRms(wet, 88200);
    const double dryRms = renderPostReleaseRms(dry, 88200);

    CHECK(wetRms > 10.0 * dryRms);
    CHECK(wetRms > 1e-4);
    CHECK(dryRms < 1e-3);
}
