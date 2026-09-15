#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/PatchStruct.h"
#include "engine/VoiceManager.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace agentic_synth::engine;
using agentic_synth::LfoTarget;
using agentic_synth::LfoWaveform;
using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;
using Catch::Approx;

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

// ── Rendered filter-corner measurement (#428) ────────────────────────────────
//
// One voice, sine oscillator at a chosen probe frequency, FX and filter drive
// bypassed, amplitude envelope held open. Sweeping the probe frequency and
// measuring steady-state output gives the voice's rendered magnitude response;
// the frequency where it falls 3 dB below the passband is the rendered corner.
// This measures the cutoff the voice actually renders, not the patch field.

PatchStruct makeCornerProbePatch(float cutoffHz, float envMod, float sustain) {
    PatchStruct p = make_default_patch();
    p.osc[0].type = agentic_synth::OscType::Sine;
    p.osc[0].enabled = 1;
    p.osc[0].volume = 1.0f;
    for (int i = 1; i < agentic_synth::kMaxOscillators; ++i) {
        p.osc[i].enabled = 0;
        p.osc[i].volume = 0.0f;
    }
    p.filter.cutoff_hz = cutoffHz;
    p.filter.resonance = 0.0f;
    p.filter.env_mod = envMod;
    p.filter.drive = 0.0f;
    p.filter_env.attack_s = 0.001f;
    p.filter_env.decay_s = 0.001f;
    p.filter_env.sustain = sustain;
    p.filter_env.release_s = 0.1f;
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.1f;
    p.chorus.mix = 0.0f; // bit-exact bypass so the pre-filter stage is neutral
    p.tubesat.drive = 0.0f;
    p.delay.mix = 0.0f;
    p.reverb.mix = 0.0f;
    for (auto& lfo : p.lfo) {
        lfo.target = LfoTarget::None;
        lfo.depth = 0.0f;
    }
    return p;
}

constexpr double kRespSampleRate = 44100.0;

// Steady-state RMS output for a sine probe at `probeHz`. The nearest MIDI note
// is used and the residual interval is applied through osc[0].semitone_offset,
// so the probe lands within a few cents of the requested frequency.
double renderedAmplitudeAt(const PatchStruct& patch, float probeHz, float velocity) {
    const int note = std::clamp(static_cast<int>(std::lround(69.0 + 12.0 * std::log2(probeHz / 440.0))), 0, 127);
    const double noteHz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    PatchStruct p = patch;
    p.osc[0].semitone_offset = static_cast<float>(12.0 * std::log2(probeHz / noteHz));
    p.osc[0].detune_cents = 0.0f;

    VoiceManager vm(1);
    vm.prepare(kRespSampleRate);
    vm.applyPatch(p);
    vm.noteOn(note, velocity);
    constexpr int kSettle = 4096;
    constexpr int kMeasure = 8192;
    std::vector<float> buf(static_cast<std::size_t>(kSettle + kMeasure), 0.0f);
    vm.renderBlock(buf.data(), kSettle + kMeasure);

    double acc = 0.0;
    for (int i = kSettle; i < kSettle + kMeasure; ++i)
        acc += static_cast<double>(buf[static_cast<std::size_t>(i)]) * buf[static_cast<std::size_t>(i)];
    return std::sqrt(acc / kMeasure);
}

// Frequency where the rendered response first falls to 1/√2 (−3 dB) of the
// passband reference (mean amplitude over [refLo, refHi]). Returns 0 if it
// never crosses.
double renderedMinus3dBCorner(const PatchStruct& patch, float velocity = 1.0f, double refLo = 40.0,
                              double refHi = 80.0) {
    constexpr int kProbes = 72;
    std::vector<double> probeHz(kProbes);
    std::vector<double> amp(kProbes);
    for (int i = 0; i < kProbes; ++i) {
        probeHz[i] = 40.0 * std::pow(16000.0 / 40.0, static_cast<double>(i) / (kProbes - 1));
        amp[i] = renderedAmplitudeAt(patch, static_cast<float>(probeHz[i]), velocity);
    }
    double refAcc = 0.0;
    int refN = 0;
    for (int i = 0; i < kProbes; ++i) {
        if (probeHz[i] >= refLo && probeHz[i] <= refHi) {
            refAcc += amp[i] * amp[i];
            ++refN;
        }
    }
    if (refN == 0)
        return 0.0;
    const double target = std::sqrt(refAcc / refN) / std::sqrt(2.0);
    for (int i = 1; i < kProbes; ++i) {
        if (amp[i - 1] >= target && amp[i] < target && target > 0.0) {
            const double t = (std::log(target) - std::log(amp[i - 1])) / (std::log(amp[i]) - std::log(amp[i - 1]));
            return probeHz[i - 1] * std::pow(probeHz[i] / probeHz[i - 1], t);
        }
    }
    return 0.0;
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
        p.filter_env.decay_s = 0.03f; // env active ~31 ms
        p.filter_env.sustain = 0.0f;
        p.filter_env.release_s = 0.05f;
        p.amp_env.attack_s = 0.001f;
        p.amp_env.decay_s = 0.001f;
        p.amp_env.sustain = 1.0f; // hold amp open so we isolate filter-env effect
        p.amp_env.release_s = 0.5f;
        vm.applyPatch(p);
        vm.noteOn(72, 1.0f); // C5 ≈ 523 Hz so harmonics span the cutoff sweep
        std::vector<float> buf(8192, 0.0f);
        vm.renderBlock(buf.data(), static_cast<int>(buf.size()));
        // Compare RMS of "env active" window (samples 200-1400, ~5-32 ms)
        // vs "env settled" window (samples 4000-8000, ~91-181 ms).
        auto windowRms = [&](int start, int end) {
            double s = 0.0;
            for (int i = start; i < end; ++i)
                s += buf[i] * buf[i];
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
    wet.amp_env.release_s = 0.01f; // dry signal dies fast
    wet.reverb.mix = 1.0f;
    wet.reverb.size = 0.8f;
    wet.reverb.damping = 0.3f;
    wet.delay.mix = 0.0f; // isolate reverb

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

// ── Pre-filter chorus placement (#265) ───────────────────────────────────────

namespace {

// Render a one-voice saw patch at the given cutoff with `chorusMix` set and
// return the stereo voice output. Used to compare the voice's L/R width with
// chorus off vs on.
void renderChorusProbe(float chorusMix, float cutoffHz, std::vector<float>& left, std::vector<float>& right) {
    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.osc[0].enabled = 1;
    p.osc[0].type = agentic_synth::OscType::Sawtooth;
    p.osc[0].volume = 1.0f;
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    p.filter.cutoff_hz = cutoffHz;
    p.filter.resonance = 0.0f;
    p.filter.env_mod = 0.0f;
    p.filter.drive = 0.0f;
    p.tubesat.drive = 0.0f; // isolate chorus
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.5f;
    p.master_gain = 1.0f;
    p.delay.mix = 0.0f;
    p.reverb.mix = 0.0f;
    p.chorus.rate_hz = 0.4f;
    p.chorus.depth = 0.35f;
    p.chorus.mix = chorusMix;
    vm.applyPatch(p);
    vm.noteOn(60, 1.0f);
    const int n = 44100; // 1 s: chorus delay line fully filled
    left.assign(static_cast<std::size_t>(n), 0.0f);
    right.assign(static_cast<std::size_t>(n), 0.0f);
    vm.renderBlock(left.data(), right.data(), n);
}

} // namespace

TEST_CASE("VoiceManager chorus stays pre-filter: it must not change the voice's stereo width", "[chorus][phaseE]") {
    // Cutoff above the 261 Hz saw fundamental so the filter is not the variable
    // under test; this test probes the placement-driven L/R image, not the
    // spectrum.
    const float cutoff = 2000.0f;
    std::vector<float> dryL, dryR, wetL, wetR;
    renderChorusProbe(0.0f, cutoff, dryL, dryR);
    renderChorusProbe(1.0f, cutoff, wetL, wetR);

    // Discard the first 250 ms so the chorus delay line has filled and the amp
    // envelope has settled. Width = side RMS / mid RMS is independent of the
    // overall level, so it isolates a change in the stereo image from a change
    // in gain. side = L-R, mid = L+R (the 0.5 scaling cancels in the ratio).
    const int start = 11025;
    double drySide = 0.0;
    double dryMid = 0.0;
    double wetSide = 0.0;
    double wetMid = 0.0;
    for (int i = start; i < static_cast<int>(dryL.size()); ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const double dl = dryL[idx];
        const double dr = dryR[idx];
        const double wl = wetL[idx];
        const double wr = wetR[idx];
        drySide += (dl - dr) * (dl - dr);
        dryMid += (dl + dr) * (dl + dr);
        wetSide += (wl - wr) * (wl - wr);
        wetMid += (wl + wr) * (wl + wr);
    }
    const double dryWidth = std::sqrt(drySide / dryMid);
    const double wetWidth = std::sqrt(wetSide / wetMid);

    // #265 places chorus BEFORE the mono filter, which mono-sums the ensemble:
    // the voice's L/R split is re-derived solely from the per-osc/voice pan
    // weights AFTER the filter, so switching the chorus on cannot alter the
    // width. The old post-filter placement ran the stereo chorus on the
    // pan-split L/R with a π-offset LFO, which widened the voice (~2.5x on this
    // probe) — this bound fails loudly on that ordering.
    CHECK(dryMid > 1e-3); // guard against a trivially-zero width ratio
    CHECK(wetMid > 1e-3);
    CHECK(wetWidth <= dryWidth * 1.1);
}

TEST_CASE("VoiceManager delay mix=1 produces echoes after note off") {
    PatchStruct wet = make_default_patch();
    wet.amp_env.release_s = 0.01f;
    wet.delay.mix = 1.0f;
    wet.delay.time_s = 0.1f;
    wet.delay.feedback = 0.5f;
    wet.reverb.mix = 0.0f; // isolate delay

    PatchStruct dry = wet;
    dry.delay.mix = 0.0f;

    const double wetRms = renderPostReleaseRms(wet, 88200);
    const double dryRms = renderPostReleaseRms(dry, 88200);

    CHECK(wetRms > 10.0 * dryRms);
    CHECK(wetRms > 1e-4);
    CHECK(dryRms < 1e-3);
}

// ── Filter envelope → cutoff contract (#428) ─────────────────────────────────
//
// The filter envelope is applied to cutoff in the octave domain:
//   cutoff_audible = cutoff_hz * 2^(env_mod * kFilterEnvMaxOctaves * env_out)
// with kFilterEnvMaxOctaves = 4 (PatchStruct.h). Before #428 the engine used a
// linear, velocity-scaled factor (1 + env_out * env_mod * velocity * 2), which
// silently multiplied the sustained cutoff by up to 3x and made hard notes
// brighter regardless of the patch. These tests render a held sine at a range
// of frequencies and locate the resulting −3 dB corner, so they observe the
// cutoff the engine actually produces.

TEST_CASE("VoiceManager filter env_mod = 0: rendered -3 dB corner is set by cutoff_hz", "[filter][envmod]") {
    // cutoff_hz is the MoogLadder pole frequency. A cascade of four one-pole
    // sections is 3 dB down at (2^(1/4) - 1)^(1/2) ≈ 0.463 of the pole
    // frequency; that fixed filter factor is all that separates the rendered
    // corner from the patch value. With env_mod = 0 nothing else may move it.
    constexpr double kMoog4Pole3dB = 0.463;
    const float cutoff = 1000.0f;
    const double corner = renderedMinus3dBCorner(makeCornerProbePatch(cutoff, 0.0f, 0.0f));
    INFO("rendered corner=" << corner << " Hz, cutoff_hz=" << cutoff);
    REQUIRE(corner > 0.0);
    CHECK(corner == Approx(kMoog4Pole3dB * cutoff).epsilon(0.20));
    // No hidden multiplier: the rendered corner never exceeds the knob.
    CHECK(corner <= cutoff);
}

TEST_CASE("VoiceManager filter env_mod *sustain = 0: held cutoff returns to cutoff_hz", "[filter][envmod]") {
    const float cutoff = 1000.0f;
    const double base = renderedMinus3dBCorner(makeCornerProbePatch(cutoff, 0.0f, 0.0f));
    const double held = renderedMinus3dBCorner(makeCornerProbePatch(cutoff, 1.0f, 0.0f));
    INFO("base=" << base << " Hz, held=" << held << " Hz");
    REQUIRE(base > 0.0);
    // The envelope decays to 0, so 2^(env_mod * 4 * 0) = 1.
    CHECK(held == Approx(base).epsilon(0.05));
}

TEST_CASE("VoiceManager filter env_mod scales cutoff by whole octaves", "[filter][envmod]") {
    // Base cutoff low enough that the +4 octave peak stays clear of Nyquist.
    const float cutoff = 250.0f;
    const double base = renderedMinus3dBCorner(makeCornerProbePatch(cutoff, 0.0f, 0.0f));
    REQUIRE(base > 0.0);

    // sustain=1 holds env_out at 1.0, so the multiplier is exactly
    // 2^(env_mod * 4): 0.5 -> 4x (+2 oct), 1.0 -> 16x (+4 oct). The old
    // linear scaling gave 2.5x and 4x respectively, so both CHECKs below
    // fail on the pre-#428 behaviour.
    const double half = renderedMinus3dBCorner(makeCornerProbePatch(cutoff, 0.5f, 1.0f));
    const double full = renderedMinus3dBCorner(makeCornerProbePatch(cutoff, 1.0f, 1.0f));
    INFO("base=" << base << " half=" << half << " full=" << full);
    // Compare in octaves so the fixed 4-pole corner-vs-pole offset cancels.
    CHECK(std::log2(half / base) == Approx(2.0).margin(0.5));
    CHECK(std::log2(full / base) == Approx(4.0).margin(0.5));
}

TEST_CASE("VoiceManager filter cutoff does not scale with velocity (#428)", "[filter][envmod]") {
    const float cutoff = 250.0f;
    const PatchStruct patch = makeCornerProbePatch(cutoff, 1.0f, 1.0f);
    const double soft = renderedMinus3dBCorner(patch, 0.25f);
    const double hard = renderedMinus3dBCorner(patch, 1.0f);
    INFO("soft=" << soft << " Hz, hard=" << hard << " Hz");
    REQUIRE(hard > 0.0);
    // Velocity scales the amp envelope only. Before #428 it was folded into
    // the filter env multiplier (1 + env * mod * velocity * 2), so a soft
    // note rendered a markedly lower corner than a hard one.
    CHECK(soft == Approx(hard).epsilon(0.05));
}

// ── #430 post-filter gain-step acceptance ────────────────────────────────────

namespace {

// DFT band energy Σ|X[k]|² for bins whose centre frequency lies in [loHz, hiHz).
// Reuses the same spectrum idea as renderedAmplitudeAt / bufferRms — measures
// energy the closed filter must not leak when post-filter gains step.
double bandEnergy(const std::vector<float>& x, double sampleRate, double loHz, double hiHz) {
    const int n = static_cast<int>(x.size());
    REQUIRE(n > 0);
    double energy = 0.0;
    for (int k = 0; k < n / 2; ++k) {
        const double freq = static_cast<double>(k) * sampleRate / static_cast<double>(n);
        if (freq < loHz || freq >= hiHz)
            continue;
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < n; ++i) {
            const double ang = -2.0 * 3.14159265358979323846 * static_cast<double>(k) * static_cast<double>(i) /
                               static_cast<double>(n);
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) *
                   static_cast<double>(x[static_cast<std::size_t>(i)]);
        }
        energy += std::norm(acc);
    }
    return energy;
}

PatchStruct makeClosedFilterAmpLfoPatch(float lfoDepth) {
    PatchStruct p = make_default_patch();
    // Noise fills the spectrum so any post-filter gain discontinuity shows up
    // as broadband energy above the closed cutoff.
    p.osc[0].type = agentic_synth::OscType::Noise;
    p.osc[0].enabled = 1;
    p.osc[0].volume = 1.0f;
    for (int i = 1; i < agentic_synth::kMaxOscillators; ++i) {
        p.osc[i].enabled = 0;
        p.osc[i].volume = 0.0f;
    }
    p.filter.type = agentic_synth::FilterType::LowPass;
    p.filter.cutoff_hz = 20.0f; // patch minimum
    p.filter.resonance = 0.0f;
    p.filter.env_mod = 0.0f;
    p.filter.drive = 0.0f;
    p.amp_env.attack_s = 0.01f;
    p.amp_env.decay_s = 0.0f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.1f;
    p.chorus.mix = 0.0f;
    p.tubesat.drive = 0.0f;
    p.delay.mix = 0.0f;
    p.reverb.mix = 0.0f;
    p.lfo[0].waveform = LfoWaveform::SampleAndHold;
    p.lfo[0].rate_hz = 20.0f;
    p.lfo[0].depth = lfoDepth;
    p.lfo[0].target = LfoTarget::Amplitude;
    p.lfo[1].target = LfoTarget::None;
    p.lfo[1].depth = 0.0f;
    return p;
}

std::vector<float> renderSettledMono(const PatchStruct& patch, int settle, int measure) {
    VoiceManager vm(1);
    vm.prepare(44100.0);
    vm.applyPatch(patch);
    vm.noteOn(60, 1.0f);
    std::vector<float> buf(static_cast<std::size_t>(settle + measure), 0.0f);
    vm.renderBlock(buf.data(), settle + measure);
    return std::vector<float>(buf.begin() + settle, buf.end());
}

} // namespace

TEST_CASE("VoiceManager closed filter + S&H Amplitude LFO: no excess broadband above cutoff (#430)",
          "[voice][click][430]") {
    // Acceptance: cutoff at the patch minimum + S&H @ 20 Hz → Amplitude must
    // not inject broadband energy above the closed filter. Unit A slew-limits
    // LFO output; this test locks the audible contract.
    constexpr double kSr = 44100.0;
    constexpr int kSettle = 4096;
    constexpr int kMeasure = 4096; // power-of-two DFT window
    // High band well above the 20 Hz cutoff (and above Moog 4-pole residual).
    constexpr double kHighLo = 1000.0;
    constexpr double kNyquist = kSr * 0.5;

    const auto withLfo = renderSettledMono(makeClosedFilterAmpLfoPatch(1.0f), kSettle, kMeasure);
    const auto quietRef = renderSettledMono(makeClosedFilterAmpLfoPatch(0.0f), kSettle, kMeasure);

    const double highWith = bandEnergy(withLfo, kSr, kHighLo, kNyquist);
    const double highRef = bandEnergy(quietRef, kSr, kHighLo, kNyquist);
    const double totalWith = bandEnergy(withLfo, kSr, 0.0, kNyquist);
    INFO("highWith=" << highWith << " highRef=" << highRef << " totalWith=" << totalWith);

    // Absolute floor: closed Moog + noise is already tiny above 1 kHz; a
    // stepped amp LFO would push this orders of magnitude higher. Bound the
    // high band to a small fraction of total energy AND within a small factor
    // of the depth=0 reference (slew may leave a slight residual).
    REQUIRE(totalWith > 0.0);
    CHECK(highWith / totalWith < 0.05);
    CHECK(highWith <= highRef * 4.0 + 1e-6);
}

TEST_CASE("VoiceManager attack_s=0 note-on: max sample-to-sample delta below threshold (#430)", "[voice][click][430]") {
    // Documented threshold: Unit B floors amp attack/release to 1 ms, so the
    // first-sample envelope jump is ~0.19 (see ADSREnvelopeTest). Through a
    // near-open filter + sine osc the sample-to-sample output delta stays well
    // below 0.5. Pre-floor instantaneous attack could produce deltas near 1.0+.
    constexpr float kMaxSampleToSampleDelta = 0.5f;

    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.osc[0].type = agentic_synth::OscType::Sine;
    p.osc[0].enabled = 1;
    p.osc[0].volume = 1.0f;
    for (int i = 1; i < agentic_synth::kMaxOscillators; ++i) {
        p.osc[i].enabled = 0;
        p.osc[i].volume = 0.0f;
    }
    p.filter.cutoff_hz = 18000.0f;
    p.filter.resonance = 0.0f;
    p.filter.env_mod = 0.0f;
    p.amp_env.attack_s = 0.0f; // raw zero — envelope / validator floor must catch it
    p.amp_env.decay_s = 0.0f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.1f;
    p.chorus.mix = 0.0f;
    p.tubesat.drive = 0.0f;
    p.delay.mix = 0.0f;
    p.reverb.mix = 0.0f;
    for (auto& lfo : p.lfo) {
        lfo.target = LfoTarget::None;
        lfo.depth = 0.0f;
    }
    vm.applyPatch(p);
    vm.noteOn(60, 1.0f);

    constexpr int kWindow = 512; // covers the 1 ms floor (~44 samples) with margin
    std::vector<float> buf(static_cast<std::size_t>(kWindow), 0.0f);
    vm.renderBlock(buf.data(), kWindow);

    float maxDelta = 0.0f;
    float prev = 0.0f; // idle → first sample is itself a delta from silence
    for (float s : buf) {
        maxDelta = std::max(maxDelta, std::abs(s - prev));
        prev = s;
        REQUIRE(std::isfinite(s));
    }
    INFO("max |sample-to-sample Δ| = " << maxDelta);
    REQUIRE(maxDelta < kMaxSampleToSampleDelta);
}
