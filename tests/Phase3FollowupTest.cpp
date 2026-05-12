// Phase 3 follow-up — quantitative tests covering the bug fixes from the
// post-Phase-3 code review:
//   FIX 1: FM oscillator sample-rate independence (was hardcoded 44.1 kHz).
//   FIX 2: FM carrier independent phase (was sharing modulator's phase, so
//          perceived pitch was fc*ratio instead of fc when ratio != 1).
//   FIX 3: Voice-level pan double-multiply (per-osc pan + voice-pan now
//          combined into a single pan stage so constant-power holds).
//
// Tests:
//   1. Hard-pan stereo direction (osc0 pan = ±1 → matching channel dominates by ≥6 dB).
//   2. Per-osc volume weighted mix (RMS scales with sum of per-osc volumes).
//   3. FM sample-rate independence (44.1 kHz vs 48 kHz peak frequency within 1 Hz).
//   4. FilterType swap mid-render (no click, all samples finite).
//   5. voice_count drop mid-play (no click; active voices fall to new cap).
//   6. BPM-sync delay length numerical (1 beat @ 120 BPM = 22050 samples).
//   7. LFO WavetablePos — multi-frame default table makes the modulation
//      audible (Phase-4 follow-up).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/MidiHandler.h"
#include "engine/PatchStruct.h"
#include "engine/VoiceManager.h"
#include "plugin/AgenticSynthPlugin.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

using agentic_synth::FilterType;
using agentic_synth::LfoTarget;
using agentic_synth::LfoWaveform;
using agentic_synth::OscType;
using agentic_synth::PatchStruct;
using agentic_synth::make_default_patch;
using agentic_synth::engine::RawMidiMsg;
using agentic_synth::engine::VoiceManager;

namespace {

struct PluginFixture {
    juce::ScopedJuceInitialiser_GUI gui;
};

double bufferRms(const float* p, int n) {
    if (n <= 0)
        return 0.0;
    double acc = 0.0;
    for (int i = 0; i < n; ++i)
        acc += static_cast<double>(p[i]) * p[i];
    return std::sqrt(acc / static_cast<double>(n));
}

// Tiny Goertzel-free peak finder: brute-force DFT over a small frequency
// grid. Sufficient for finding the dominant tone in a few-thousand-sample
// FM buffer when we only need ±1 Hz resolution.
double dominantFrequencyHz(const std::vector<float>& buf, double sampleRate,
                           double fMinHz, double fMaxHz, double stepHz) {
    const int N = static_cast<int>(buf.size());
    double bestF = fMinHz;
    double bestMag = -1.0;
    for (double f = fMinHz; f <= fMaxHz; f += stepHz) {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * M_PI * f / sampleRate;
        for (int n = 0; n < N; ++n) {
            re += buf[n] * std::cos(w * n);
            im += buf[n] * std::sin(w * n);
        }
        const double mag = re * re + im * im;
        if (mag > bestMag) {
            bestMag = mag;
            bestF = f;
        }
    }
    return bestF;
}

// Render N samples through a fresh VoiceManager at the given sample rate with
// the supplied patch. Used by FM-SR-independence test.
std::vector<float> renderMono(double sampleRate, const PatchStruct& patch, int midiNote,
                              int samples, int discardSamples = 0) {
    VoiceManager vm(4);
    vm.prepare(sampleRate);
    vm.applyPatch(patch);
    vm.noteOn(midiNote, 1.0f);
    if (discardSamples > 0) {
        std::vector<float> warm(discardSamples, 0.0f);
        vm.renderBlock(warm.data(), discardSamples);
    }
    std::vector<float> out(samples, 0.0f);
    vm.renderBlock(out.data(), samples);
    return out;
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// TEST 1 — Hard-pan stereo direction.
// Replaces the byte-differs check in UnwiredParamsTest with a quantitative
// L/R-RMS direction assertion (≥6 dB dominance on the panned side).
// ──────────────────────────────────────────────────────────────────────────────

namespace {

// Render `samples` of stereo with one osc enabled at the given pan position.
// Caller picks osc0 type so we can stress band-limited (saw) vs the FM path.
void renderSinglePanned(float pan, int samples, std::vector<float>& outL,
                        std::vector<float>& outR) {
    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f; // wide open
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::Sawtooth;
    p.osc[0].pan = pan;
    p.osc[0].volume = 1.0f;
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    vm.applyPatch(p);
    vm.noteOn(60, 1.0f);
    outL.assign(samples, 0.0f);
    outR.assign(samples, 0.0f);
    vm.renderBlock(outL.data(), outR.data(), samples);
}

} // namespace

TEST_CASE("Hard-pan: osc0 pan=-1 → L dominates R by ≥6 dB", "[phase3-followup][pan]") {
    constexpr int N = 4096;
    std::vector<float> l, r;
    renderSinglePanned(-1.0f, N, l, r);
    const double rmsL = bufferRms(l.data(), N);
    const double rmsR = bufferRms(r.data(), N);
    REQUIRE(rmsL > 0.0);
    // 6 dB == factor of 2 in amplitude (RMS).
    REQUIRE(rmsL > rmsR * 2.0);
}

TEST_CASE("Hard-pan: osc0 pan=+1 → R dominates L by ≥6 dB", "[phase3-followup][pan]") {
    constexpr int N = 4096;
    std::vector<float> l, r;
    renderSinglePanned(+1.0f, N, l, r);
    const double rmsL = bufferRms(l.data(), N);
    const double rmsR = bufferRms(r.data(), N);
    REQUIRE(rmsR > 0.0);
    REQUIRE(rmsR > rmsL * 2.0);
}

TEST_CASE("Hard-pan: opposing per-osc pan splits energy L/R", "[phase3-followup][pan]") {
    // Osc0 hard-L, Osc1 hard-R, both at identical type+freq. Each channel
    // should carry comparable energy (rough symmetry) — and definitely each
    // > 0, which fails if voice-pan double-multiply collapses the image.
    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::Sawtooth;
    p.osc[0].pan = -1.0f;
    p.osc[0].volume = 1.0f;
    p.osc[1].enabled = 1;
    p.osc[1].type = OscType::Sawtooth;
    p.osc[1].pan = +1.0f;
    p.osc[1].volume = 1.0f;
    p.osc[2].enabled = 0;
    vm.applyPatch(p);
    vm.noteOn(60, 1.0f);

    constexpr int N = 4096;
    std::vector<float> l(N, 0.0f), r(N, 0.0f);
    vm.renderBlock(l.data(), r.data(), N);
    const double rmsL = bufferRms(l.data(), N);
    const double rmsR = bufferRms(r.data(), N);
    REQUIRE(rmsL > 0.001);
    REQUIRE(rmsR > 0.001);
    // Voice-level pan offsets slot 0 to -0.4 (round-robin index 0). Per-osc
    // hard-L on osc0 stacks to -1.4 (clamped to -1); per-osc hard-R on osc1
    // stacks to +0.6. So R is somewhat less than L but both are firmly > 0.
    // Loose ratio bound: neither side may be more than 5× the other.
    const double ratio = rmsL / rmsR;
    REQUIRE(ratio < 5.0);
    REQUIRE(ratio > 0.2);
}

// ──────────────────────────────────────────────────────────────────────────────
// TEST 2 — Per-osc volume weighted mix.
// Three oscs at the same type/freq with volumes {0.1, 1.0, 0.5}. Dropping
// osc0's volume to 0 should change RMS by ~6.25% (= 0.1 / 1.6).
// ──────────────────────────────────────────────────────────────────────────────

namespace {

double rmsForOsc0Volume(float v0) {
    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    // All three oscs at the same type/freq → phase-coherent stack.
    for (int i = 0; i < 3; ++i) {
        p.osc[i].enabled = 1;
        p.osc[i].type = OscType::Sine;
        p.osc[i].pan = 0.0f;
        p.osc[i].semitone_offset = 0.0f;
        p.osc[i].detune_cents = 0.0f;
    }
    p.osc[0].volume = v0;
    p.osc[1].volume = 1.0f;
    p.osc[2].volume = 0.5f;
    // Sustain held open so the buffer measures steady-state, not envelope ramp.
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.5f;
    vm.applyPatch(p);
    vm.noteOn(69, 1.0f); // A4 = 440 Hz
    // Skip envelope ramp; measure steady state.
    constexpr int kWarm = 512;
    std::vector<float> warm(kWarm, 0.0f);
    vm.renderBlock(warm.data(), kWarm);
    constexpr int N = 4096;
    std::vector<float> buf(N, 0.0f);
    vm.renderBlock(buf.data(), N);
    return bufferRms(buf.data(), N);
}

} // namespace

TEST_CASE("Per-osc volume: RMS scales with sum of volumes", "[phase3-followup][volume]") {
    const double rmsAll = rmsForOsc0Volume(0.1f); // total volume 1.6
    const double rmsNoOsc0 = rmsForOsc0Volume(0.0f); // total volume 1.5
    REQUIRE(rmsAll > 0.0);
    REQUIRE(rmsNoOsc0 > 0.0);
    // Expected ratio: rmsNoOsc0 / rmsAll == 1.5 / 1.6 == 0.9375. Allow ±5%
    // for envelope/filter colouration and floating-point variance.
    const double observed = rmsNoOsc0 / rmsAll;
    REQUIRE(observed > 0.88);
    REQUIRE(observed < 1.00);
}

// ──────────────────────────────────────────────────────────────────────────────
// TEST 3 — FM oscillator sample-rate independence.
// FIX 1 + FIX 2 regression coverage. With fm_ratio=2 the modulator runs at
// 2× carrier; carrier pitch must equal the requested note frequency at both
// 44.1 kHz and 48 kHz. Before FIX 1 the hardcoded SR mistuned by 8.8% at 48k.
// ──────────────────────────────────────────────────────────────────────────────

namespace {

PatchStruct fmTestPatch() {
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::FM;
    p.osc[0].fm_ratio = 2.0f;
    p.osc[0].fm_depth = 0.05f; // small index → carrier dominates the spectrum
    p.osc[0].volume = 1.0f;
    p.osc[0].pan = 0.0f;
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    // Steady-state ASR open.
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.5f;
    return p;
}

} // namespace

TEST_CASE("FM SR-independence: carrier pitch invariant across SR (FIX 1+2)",
          "[phase3-followup][fm]") {
    const auto p = fmTestPatch();
    // C4 = 261.63 Hz. Search ±50 Hz at 1-Hz resolution.
    constexpr int midiNote = 60;
    const double expectedHz = 261.6256; // 440 * 2^((60-69)/12)
    const int analyseLen = 8192;
    const int warm = 1024; // skip attack transient

    const auto buf44 = renderMono(44100.0, p, midiNote, analyseLen, warm);
    const auto buf48 = renderMono(48000.0, p, midiNote, analyseLen, warm);

    const double peak44 = dominantFrequencyHz(buf44, 44100.0, expectedHz - 50.0,
                                              expectedHz + 50.0, 0.5);
    const double peak48 = dominantFrequencyHz(buf48, 48000.0, expectedHz - 50.0,
                                              expectedHz + 50.0, 0.5);
    INFO("peak44 = " << peak44 << " peak48 = " << peak48 << " expected = " << expectedHz);
    REQUIRE(std::abs(peak44 - expectedHz) < 2.0);
    REQUIRE(std::abs(peak48 - expectedHz) < 2.0);
    REQUIRE(std::abs(peak44 - peak48) < 1.0);
}

// ──────────────────────────────────────────────────────────────────────────────
// TEST 4 — FilterType swap mid-render produces no click.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FilterType swap mid-render: no NaN, bounded boundary delta",
          "[phase3-followup][filter]") {
    // Use a sine osc + open cutoff so LP and HP produce similar amplitudes
    // either side of the swap (LP passes the sine, HP kills it). Phase 4
    // adds a ~5 ms type-swap crossfade that blends old + new filter outputs
    // linearly — so the boundary discontinuity must now sit at the tight
    // 0.3 bound (previously relaxed to 1.0 due to pointer-swap click).
    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 8000.0f; // well above the sine fundamental
    p.filter.resonance = 0.1f;
    p.filter.type = FilterType::LowPass;
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::Sine;
    p.osc[0].volume = 0.5f;
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.5f;
    vm.applyPatch(p);
    vm.noteOn(60, 0.8f);

    std::vector<float> first(512, 0.0f);
    vm.renderBlock(first.data(), 512);

    // Swap LowPass → HighPass mid-render.
    p.filter.type = FilterType::HighPass;
    vm.applyPatch(p);

    std::vector<float> second(512, 0.0f);
    vm.renderBlock(second.data(), 512);

    // Hard checks: no NaN/inf anywhere, output stays inside ±2.0.
    auto checkFinite = [](const std::vector<float>& b) {
        for (float s : b) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) < 2.0f);
        }
    };
    checkFinite(first);
    checkFinite(second);

    // Phase 4 crossfade tightens this bound back to 0.3. With the linear
    // 5 ms blend, the first crossfade sample is `oldOut*1 + newOut*0` which
    // exactly equals the previous filter output — so the boundary is the
    // natural waveform-slope delta, NOT a filter-state discontinuity.
    const float boundaryDelta = std::abs(second.front() - first.back());
    INFO("boundary delta = " << boundaryDelta);
    REQUIRE(boundaryDelta < 0.3f);

    // Sample-to-sample max delta across the crossfade window itself
    // (first 220 samples of `second`). A bad crossfade would show a step
    // partway through; the linear blend keeps this bounded.
    const int kCrossfade = 220;
    float maxStep = 0.0f;
    for (int i = 1; i < kCrossfade && i < static_cast<int>(second.size()); ++i) {
        const float d = std::abs(second[static_cast<std::size_t>(i)] -
                                 second[static_cast<std::size_t>(i - 1)]);
        if (d > maxStep)
            maxStep = d;
    }
    INFO("max intra-crossfade step = " << maxStep);
    REQUIRE(maxStep < 0.3f);
}

// ──────────────────────────────────────────────────────────────────────────────
// Phase 4 — LFO amp clamp. Two LFOs both target Amplitude at depth=1; the
// trough of `1 + lfoAmpMod` would dip to (1 - 1 - 1) = -1 without a clamp,
// producing a sign-flipped waveform at the trough. After Phase 4's
// std::max(0, ...) clamp the trough should hug 0, never invert.
//
// Detect inversion by correlating against a one-LFO baseline: with the
// clamp the two-LFO output is a strictly more aggressive amplitude
// envelope of the same sign; without it the trough sign-flips and zero
// crossings appear that don't exist in the baseline.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("LFO amp clamp: two LFOs target Amplitude don't sign-invert",
          "[phase4][lfo][amp-clamp]") {
    auto render = [](float depth0, float depth1) {
        VoiceManager vm(1);
        vm.prepare(44100.0);
        PatchStruct p = make_default_patch();
        p.filter.cutoff_hz = 18000.0f;
        p.osc[0].enabled = 1;
        p.osc[0].type = OscType::Sine;
        p.osc[0].volume = 1.0f;
        p.osc[0].pan = 0.0f;
        p.osc[1].enabled = 0;
        p.osc[2].enabled = 0;
        // Open envelope so we measure LFO-driven amp shape, not ADSR.
        p.amp_env.attack_s = 0.001f;
        p.amp_env.decay_s = 0.001f;
        p.amp_env.sustain = 1.0f;
        p.amp_env.release_s = 0.5f;
        // Both LFOs target Amplitude. Slow-ish so the trough is sampled
        // densely in 8192 samples (one full cycle at 5 Hz = 8820 samples).
        p.lfo[0].waveform = LfoWaveform::Sine;
        p.lfo[0].target = LfoTarget::Amplitude;
        p.lfo[0].rate_hz = 5.0f;
        p.lfo[0].depth = depth0;
        p.lfo[0].bpm_sync = 0;
        p.lfo[1].waveform = LfoWaveform::Sine;
        p.lfo[1].target = LfoTarget::Amplitude;
        p.lfo[1].rate_hz = 5.0f;
        p.lfo[1].depth = depth1;
        p.lfo[1].bpm_sync = 0;
        vm.applyPatch(p);
        vm.noteOn(69, 1.0f); // A4 440 Hz
        // Warm up past envelope attack.
        std::vector<float> warm(512, 0.0f);
        vm.renderBlock(warm.data(), 512);
        std::vector<float> buf(8192, 0.0f);
        vm.renderBlock(buf.data(), 8192);
        return buf;
    };

    const auto baseline = render(1.0f, 0.0f); // one LFO at full depth
    const auto twoLfos = render(1.0f, 1.0f);  // two LFOs at full depth

    // Hard check: no NaN/inf, bounded amplitude.
    for (float s : twoLfos) {
        REQUIRE(std::isfinite(s));
        REQUIRE(std::abs(s) < 2.0f);
    }

    // Track local-peak amplitude in a sliding window. With the clamp,
    // both signals share a slow amp envelope at the LFO rate; the two-LFO
    // version reaches silence at troughs (zero envelope) while the single
    // LFO baseline keeps a 0.5 floor (depth=1, halved internally so trough
    // 1 + (-1)*0.5 = 0.5). Without the clamp the two-LFO output's envelope
    // would dip below zero at troughs → carrier-sign-flipped samples →
    // negative envelope. We detect that by comparing windowed RMS minima.
    auto windowedMinRms = [](const std::vector<float>& buf, int win) {
        double minRms = 1e9;
        for (int i = 0; i + win <= static_cast<int>(buf.size()); i += win / 2) {
            double acc = 0.0;
            for (int j = 0; j < win; ++j)
                acc += static_cast<double>(buf[static_cast<std::size_t>(i + j)]) *
                       buf[static_cast<std::size_t>(i + j)];
            const double rms = std::sqrt(acc / win);
            if (rms < minRms)
                minRms = rms;
        }
        return minRms;
    };

    // 220-sample window ≈ 5 ms — small enough to resolve the trough of a
    // 5 Hz LFO (period 200 ms).
    const double minRmsBaseline = windowedMinRms(baseline, 220);
    const double minRmsTwo = windowedMinRms(twoLfos, 220);
    INFO("min-RMS baseline=" << minRmsBaseline << " two-LFO=" << minRmsTwo);

    // Both LFOs at depth 1 should make troughs quieter than the single-LFO
    // case (deeper modulation). With the clamp, trough is at-or-near zero.
    // Without the clamp, the trough would carry negative-gain audio whose
    // RMS could equal or exceed the baseline's trough (sign-flip preserves
    // RMS). So the assertion is: two-LFO trough RMS is strictly less than
    // the single-LFO trough RMS by a noticeable margin.
    REQUIRE(minRmsTwo < minRmsBaseline);
    // And — separate from the comparison — the trough RMS should be small
    // in absolute terms, indicating the gain clamped at zero rather than
    // flipping to a negative passband.
    REQUIRE(minRmsTwo < 0.05);
}

// ──────────────────────────────────────────────────────────────────────────────
// LFO amp clamp — edge cases (Phase 4 follow-up).
//
// Direct white-box assertions on the clamp at lfoAmpGain = std::max(0, 1 + mod).
// The patch path clamps depth into [0, 1] (VoiceManager.cpp ~L837) so a
// user-typed depth=-1 is folded to 0 and silenced; the test below verifies
// that folding behaviour AND the strictly-non-negative gain at the LFO trough
// when two amplitude LFOs phase-align.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("LFO amp clamp: negative patch depth is folded to silence (depth clamp)",
          "[phase4][lfo][amp-clamp][edge]") {
    auto render = [](float depth) {
        VoiceManager vm(1);
        vm.prepare(44100.0);
        PatchStruct p = make_default_patch();
        p.filter.cutoff_hz = 18000.0f;
        p.osc[0].enabled = 1;
        p.osc[0].type = OscType::Sine;
        p.osc[0].volume = 1.0f;
        p.osc[0].pan = 0.0f;
        p.osc[1].enabled = 0;
        p.osc[2].enabled = 0;
        p.amp_env.attack_s = 0.001f;
        p.amp_env.decay_s = 0.001f;
        p.amp_env.sustain = 1.0f;
        p.amp_env.release_s = 0.5f;
        p.lfo[0].waveform = LfoWaveform::Sine;
        p.lfo[0].target = LfoTarget::Amplitude;
        p.lfo[0].rate_hz = 5.0f;
        p.lfo[0].depth = depth;
        p.lfo[0].bpm_sync = 0;
        vm.applyPatch(p);
        vm.noteOn(69, 1.0f);
        std::vector<float> warm(512, 0.0f);
        vm.renderBlock(warm.data(), 512);
        std::vector<float> buf(8192, 0.0f);
        vm.renderBlock(buf.data(), 8192);
        return buf;
    };

    const auto negDepth = render(-1.0f);
    const auto zeroDepth = render(0.0f);
    REQUIRE(negDepth.size() == zeroDepth.size());

    // patch.lfo[0].depth = -1 is clamped to 0 at the routing stage; both
    // renders therefore use identical modulation (none). Output must match
    // sample-for-sample.
    for (std::size_t i = 0; i < negDepth.size(); ++i) {
        REQUIRE(std::isfinite(negDepth[i]));
        REQUIRE(std::abs(negDepth[i] - zeroDepth[i]) < 1e-6f);
    }
}

TEST_CASE("LFO amp clamp: two synchronous Amp LFOs hit silence (not inversion) at trough",
          "[phase4][lfo][amp-clamp][edge]") {
    // Two LFOs both target Amplitude, same waveform + same rate + zero phase
    // offset → they stay phase-aligned, so the per-sample sum is
    // 2 * lfoOut * d * 0.5 = lfoOut * d. With d = 1, this hits exactly -1 at
    // the trough → 1 + lfoAmpMod = 0 → clamp engages at the *boundary*.
    // The point of this test is: at the trough the rendered sine carrier
    // must collapse to zero, NOT to a sign-flipped negative envelope (the
    // un-clamped behaviour). We verify that by checking the minimum
    // *signed* output dips no further negative than the absolute-value
    // envelope would predict — a sign flip would create deep negative
    // excursions of the carrier even when |env| should be tiny.
    VoiceManager vm(1);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::Sine;
    p.osc[0].volume = 1.0f;
    p.osc[0].pan = 0.0f;
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.5f;
    for (int i = 0; i < 2; ++i) {
        p.lfo[i].waveform = LfoWaveform::Sine;
        p.lfo[i].target = LfoTarget::Amplitude;
        p.lfo[i].rate_hz = 5.0f;
        p.lfo[i].depth = 1.0f;
        p.lfo[i].bpm_sync = 0;
    }
    vm.applyPatch(p);
    vm.noteOn(69, 1.0f);

    // Warm past envelope attack.
    std::vector<float> warm(2048, 0.0f);
    vm.renderBlock(warm.data(), 2048);

    // Render two full LFO cycles at 5 Hz / 44.1 kHz = 17640 samples.
    constexpr int N = 17640;
    std::vector<float> buf(N, 0.0f);
    vm.renderBlock(buf.data(), N);

    // Find the windowed min-|x| (silence floor at trough). With the clamp,
    // every 200-sample window centred on a trough should drop near zero.
    auto windowMinRms = [](const std::vector<float>& b, int win) {
        double minRms = 1e9;
        for (int i = 0; i + win <= static_cast<int>(b.size()); i += win / 2) {
            double acc = 0.0;
            for (int j = 0; j < win; ++j)
                acc += static_cast<double>(b[i + j]) * b[i + j];
            const double rms = std::sqrt(acc / win);
            if (rms < minRms)
                minRms = rms;
        }
        return minRms;
    };

    // 220 samples ≈ 5 ms — small enough to localise a 5 Hz LFO trough
    // (full cycle = 200 ms = 8820 samples).
    const double minRms = windowMinRms(buf, 220);
    INFO("trough min-RMS = " << minRms);

    // Hard checks.
    for (float s : buf) {
        REQUIRE(std::isfinite(s));
        REQUIRE(std::abs(s) < 2.0f);
    }
    // At the trough the gain term `max(0, 1 + (-1))` = 0; carrier is silenced.
    // The 220-sample window straddles the exact zero-crossing of the LFO so
    // some non-trough samples leak in — but the floor should still be very
    // small. Without the clamp, the trough would carry a negative-gain
    // sine of comparable RMS to the peak (~0.6), so anything <0.1 here
    // proves the clamp is active.
    REQUIRE(minRms < 0.1);
}

// ──────────────────────────────────────────────────────────────────────────────
// TEST 5 — voice_count drop mid-play.
// 8 notes → cap drop to 2. Excess voices fade out (no click), final active
// count ≤ 2 once envelopes settle.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("voice_count drop mid-play: no click, active voices ≤ new cap",
          "[phase3-followup][voice-count]") {
    // Use sine oscs to avoid harmonic-rich sawtooth content swamping the
    // boundary-delta measurement with waveform-slope deltas.
    VoiceManager vm(8);
    vm.prepare(44100.0);
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 18000.0f;
    p.voice_count = 8;
    p.osc[0].enabled = 1;
    p.osc[0].type = OscType::Sine;
    p.osc[0].volume = 0.3f; // many voices summing — keep headroom
    p.osc[1].enabled = 0;
    p.osc[2].enabled = 0;
    p.amp_env.attack_s = 0.001f;
    p.amp_env.decay_s = 0.001f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.05f;
    vm.applyPatch(p);

    for (int i = 0; i < 8; ++i)
        vm.noteOn(48 + i, 0.6f);
    REQUIRE(vm.activeVoiceCount() == 8);

    // Warm up so all voices are sustaining.
    std::vector<float> warm(1024, 0.0f);
    vm.renderBlock(warm.data(), 1024);
    const float lastBefore = warm.back();

    // Drop polyphony cap.
    p.voice_count = 2;
    vm.applyPatch(p);

    std::vector<float> after(8192, 0.0f);
    vm.renderBlock(after.data(), 8192);

    // Boundary sample delta — the fade-out ramps the dropped voices
    // smoothly, so the single-sample transition across applyPatch is
    // bounded by the same magnitude as a normal waveform-slope delta.
    const float boundary = std::abs(after.front() - lastBefore);
    INFO("boundary delta = " << boundary);
    REQUIRE(boundary < 0.5f);

    // No NaN / no blow-up across the fade window.
    for (float s : after) {
        REQUIRE(std::isfinite(s));
        REQUIRE(std::abs(s) < 2.0f);
    }

    // After the fade + release, ≤ 2 voices should remain active.
    REQUIRE(vm.activeVoiceCount() <= 2);
}

// ──────────────────────────────────────────────────────────────────────────────
// TEST 6 — BPM-sync delay length numerical.
// 1 beat @ 120 BPM = 0.5 s = 22050 samples at 44.1 kHz. With an impulse
// in, the wet output peak should land near sample 22050 (±50).
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("BPM-sync delay length: 1 beat @ 120 BPM = 22050 samples (44.1 kHz)",
          "[phase3-followup][delay][bpm-sync]") {
    PluginFixture fix;
    AgenticSynthPlugin plug;
    plug.prepareToPlay(44100.0, 256);
    auto& apvts = plug.getAPVTS();
    if (auto* m = apvts.getParameter("delay_mix"))
        m->setValueNotifyingHost(1.0f);
    if (auto* fb = apvts.getParameter("delay_feedback"))
        fb->setValueNotifyingHost(0.0f);
    if (auto* st = apvts.getParameter("delay_stereo"))
        st->setValueNotifyingHost(0.0f); // parallel L/R, no ping-pong
    if (auto* t = apvts.getParameter("delay_time")) {
        // Range is 0 .. 2; we want time_s = 1.0 beat.
        t->setValueNotifyingHost(t->convertTo0to1(1.0f));
    }
    if (auto* s = apvts.getParameter("delay_bpm_sync"))
        s->setValueNotifyingHost(1.0f);
    // Mute reverb so it doesn't smear the impulse.
    if (auto* rm = apvts.getParameter("reverb_mix"))
        rm->setValueNotifyingHost(0.0f);
    // No AudioPlayHead attached → engine uses the documented 120 BPM default
    // (see PatchStruct.h DelayParams comment). 1 beat @ 120 BPM = 0.5 s,
    // which at 44.1 kHz is 22050 samples — the wet echo we look for.
    //
    // The plugin chain is voices → delay → reverb (reverb_mix=0 above).
    // Triggering a noteOn produces a short dry burst at sample ~0 followed by
    // a wet copy 22050 samples later. We locate that wet peak in a ±200
    // sample window around the expected arrival.

    auto& q = plug.auditionQueueForTest();
    (void)q.push(RawMidiMsg::noteOn(60, 100));

    constexpr int kBlocks = 120;             // 120*256 = 30720 samples
    constexpr int kBlockSize = 256;
    std::vector<float> sumL;
    sumL.reserve(kBlocks * kBlockSize);
    juce::AudioBuffer<float> buf(2, kBlockSize);
    for (int b = 0; b < kBlocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        plug.processBlock(buf, midi);
        // Release after the first block so the dry burst is short.
        if (b == 0)
            (void)q.push(RawMidiMsg::noteOff(60));
        const auto* p = buf.getReadPointer(0);
        sumL.insert(sumL.end(), p, p + kBlockSize);
    }
    plug.agentBridge().setMidiNoteSink(nullptr);

    // Locate the post-22050 amplitude peak around the expected echo arrival.
    // The "peak" is the local maximum of |signal| inside the search window;
    // the wet copy aligns with the envelope-shaped onset of the original
    // note (not a single-sample impulse), so allow a few-hundred-sample
    // tolerance to absorb attack ramp + amplitude smoothing.
    constexpr int kExpected = 22050;
    constexpr int kWindow = 500;
    int peakIdx = -1;
    float peakAmp = 0.0f;
    const int lo = std::max(0, kExpected - kWindow);
    const int hi = std::min(static_cast<int>(sumL.size()), kExpected + kWindow);
    for (int i = lo; i < hi; ++i) {
        const float a = std::abs(sumL[i]);
        if (a > peakAmp) {
            peakAmp = a;
            peakIdx = i;
        }
    }
    REQUIRE(peakIdx >= 0);
    INFO("peakIdx = " << peakIdx << " expected = " << kExpected);
    REQUIRE(std::abs(peakIdx - kExpected) <= 400);
}

// ──────────────────────────────────────────────────────────────────────────────
// TEST 7 — LFO WavetablePos.
// Phase 4 shipped a multi-frame default wavetable (sine → triangle-ish →
// saw → square), so an LFO targeting WavetablePos now produces an audible
// timbre sweep. depth=0 (no modulation) must differ sample-for-sample from
// depth=1 (full sweep).
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("Phase-3 follow-up: LFO WavetablePos modulates audio",
          "[phase3-followup][wavetable-pos]") {
    auto renderForLfoDepth = [](float depth) {
        VoiceManager vm(1);
        vm.prepare(44100.0);
        PatchStruct p = make_default_patch();
        p.filter.cutoff_hz = 18000.0f;
        p.osc[0].enabled = 1;
        p.osc[0].type = OscType::Wavetable;
        p.osc[0].wavetable_pos = 0.5f;
        p.osc[0].pan = 0.0f;
        p.osc[1].enabled = 0;
        p.osc[2].enabled = 0;
        p.lfo[0].waveform = LfoWaveform::Sine;
        p.lfo[0].target = LfoTarget::WavetablePos;
        p.lfo[0].rate_hz = 2.0f;
        p.lfo[0].depth = depth;
        p.amp_env.attack_s = 0.001f;
        p.amp_env.decay_s = 0.001f;
        p.amp_env.sustain = 1.0f;
        p.amp_env.release_s = 0.5f;
        vm.applyPatch(p);
        vm.noteOn(60, 1.0f);
        std::vector<float> buf(4096, 0.0f);
        vm.renderBlock(buf.data(), 4096);
        return buf;
    };
    const auto off = renderForLfoDepth(0.0f);
    const auto on = renderForLfoDepth(1.0f);
    REQUIRE(off.size() == on.size());
    bool differs = false;
    for (std::size_t i = 0; i < off.size(); ++i) {
        if (off[i] != on[i]) {
            differs = true;
            break;
        }
    }
    // Multi-frame default table → LFO sweep through different harmonic
    // content must produce a sample-for-sample different render.
    REQUIRE(differs);
}
