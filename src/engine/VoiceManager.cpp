#include "engine/VoiceManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace agentic_synth::engine {

namespace {

// Round-robin per-voice pan offsets. Phase 2 stereo: each new voice in a
// chord gets a distinct lateral position so polyphonic content spreads
// across the field instead of summing to the center. Pattern alternates
// L/R with shrinking magnitude so the first two voices are widest and
// later voices fill the middle. Voice 0 → -0.4, 1 → +0.4, 2 → -0.2, 3 →
// +0.2, 4 → -0.6, 5 → +0.6, 6 → -0.1, 7 → +0.1, repeat for index ≥ 8.
constexpr float kVoicePanTable[] = {
    -0.4f, +0.4f, -0.2f, +0.2f, -0.6f, +0.6f, -0.1f, +0.1f,
};

float panForVoiceIndex(std::size_t i) noexcept {
    return kVoicePanTable[i % (sizeof(kVoicePanTable) / sizeof(kVoicePanTable[0]))];
}

// Constant-power pan law. p ∈ [-1, +1]; map to angle θ ∈ [0, π/2] via
// θ = ((p+1)/2) * π/2 so:
//   p = -1 → θ = 0     → L = cos 0 = 1,         R = sin 0 = 0
//   p =  0 → θ = π/4   → L = R = cos(π/4) = √2/2 ≈ 0.7071
//   p = +1 → θ = π/2   → L = cos(π/2) = 0,      R = sin(π/2) = 1
// L²+R² = cos²θ + sin²θ = 1 for all p, so total power is preserved
// regardless of pan position (the perceptual luxury of constant-power).
void computePanGains(float pan, float& l, float& r) noexcept {
    pan = std::clamp(pan, -1.0f, 1.0f);
    constexpr float kHalfPi = 1.57079632679f;
    const float theta = ((pan + 1.0f) * 0.5f) * kHalfPi;
    l = std::cos(theta);
    r = std::sin(theta);
}

LfoShape toLfoShape(LfoWaveform w) noexcept {
    switch (w) {
    case LfoWaveform::Sine:
        return LfoShape::Sine;
    case LfoWaveform::Triangle:
        return LfoShape::Triangle;
    case LfoWaveform::Sawtooth:
        return LfoShape::Saw;
    case LfoWaveform::Square:
        return LfoShape::Square;
    case LfoWaveform::SampleAndHold:
        return LfoShape::SampleAndHold;
    }
    return LfoShape::Sine;
}

} // namespace

// ── Voice ─────────────────────────────────────────────────────────────────────

namespace {

// Convert per-osc semitone + cents offset into a frequency multiplier.
inline float oscFrequencyMultiplier(float semitoneOffset, float detuneCents) noexcept {
    const float semis = semitoneOffset + detuneCents * (1.0f / 100.0f);
    return std::exp2(semis * (1.0f / 12.0f));
}

// Cheap xorshift32 → [-1, +1] white noise.
inline float xorshiftNoise(uint32_t& state) noexcept {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return (static_cast<float>(x) / 2147483648.0f) - 1.0f;
}

// Map OscType → VAOscillator::Waveform for the VA-renderable subset.
VAOscillator::Waveform toVaWaveform(OscType t) noexcept {
    switch (t) {
    case OscType::Triangle:
        return VAOscillator::Waveform::Triangle;
    case OscType::Square:
    case OscType::Pulse: // Pulse uses square waveform; pulse_width tweak applied separately
        return VAOscillator::Waveform::Square;
    case OscType::Sawtooth:
    default:
        return VAOscillator::Waveform::Saw;
    }
}

} // namespace

void Voice::prepare(double sampleRate) {
    for (auto& o : oscs) {
        o.wavetableOsc.setSampleRate(sampleRate);
        o.vaOsc.prepare(sampleRate);
        o.fmPhase = 0.0;
        o.carrierPhase = 0.0;
        o.sampleRate = static_cast<float>(sampleRate);
    }
    if (moogFilter)
        moogFilter->prepare(sampleRate);
    if (svFilter)
        svFilter->prepare(sampleRate);
    // setSampleRate only recomputes coefficients — it does not zero stage_ /
    // output_. A host that calls prepareToPlay between transports (or with a
    // new sample rate) must not leak the previous envelope stage. Same for
    // the DC blocker, whose y_prev_ can hold a non-zero residual.
    ampEnv.setSampleRate(sampleRate);
    ampEnv.reset();
    filterEnv.setSampleRate(sampleRate);
    filterEnv.reset();
    for (auto& lfo : lfos)
        lfo.setSampleRate(sampleRate);
    dcBlockerL.prepare(static_cast<float>(sampleRate));
    dcBlockerR.prepare(static_cast<float>(sampleRate));
    dcBlockerL.reset();
    dcBlockerR.reset();
    if (moogFilter)
        moogFilter->reset();
    if (svFilter)
        svFilter->reset();
    driveSmoother.setSampleRate(sampleRate);
}

void Voice::renderStereo(float portamentoAlpha, float baseCutoffHz, float resonance,
                         float& outL, float& outR) noexcept {
    if (!isActive()) {
        outL = 0.0f;
        outR = 0.0f;
        return;
    }

    // One-pole smoother: currentFrequency glides toward targetFrequency.
    currentFrequency = portamentoAlpha * currentFrequency + (1.0f - portamentoAlpha) * targetFrequency;

    // ── Modulation pass: evaluate the 2 LFOs once per sample ─────────────
    // Each LfoTarget aggregates contributions across the 2 LFOs. Pitch /
    // FilterCutoff / Amplitude existed before Phase 3; Pan / WavetablePos /
    // FmRatio modulate per-osc state on EVERY enabled osc (Phase 5: dropped
    // the slot-0 gate — standard synth behaviour is for one LFO routed to
    // a per-osc target to affect all oscs uniformly).
    float lfoPitchSemis = 0.0f;
    float lfoCutoffMod = 0.0f; // multiplicative around 1.0
    float lfoAmpMod = 0.0f;    // multiplicative around 1.0
    float lfoPanMod = 0.0f;    // additive to per-osc pan (all oscs)
    float lfoWtMod = 0.0f;     // additive to wavetable_pos (all oscs)
    float lfoFmRatioMod = 0.0f; // multiplicative around 1.0 on fm_ratio (all oscs)
    for (size_t i = 0; i < lfos.size(); ++i) {
        const float lfoOut = lfos[i].processSample(); // [-depth, +depth]
        const float d = lfoDepths[i];
        switch (lfoTargets[i]) {
        case LfoTarget::Pitch:
            lfoPitchSemis += lfoOut * d * 12.0f; // ±depth octaves max
            break;
        case LfoTarget::FilterCutoff:
            lfoCutoffMod += lfoOut * d; // ±d around 0
            break;
        case LfoTarget::Amplitude:
            lfoAmpMod += lfoOut * d * 0.5f; // halved so trough never silences voice
            break;
        case LfoTarget::Pan:
            // Modulate voice-level pan position, ±d around the assigned base.
            lfoPanMod += lfoOut * d;
            break;
        case LfoTarget::WavetablePos:
            // Modulate slot-0 wavetable morph position, ±d around base.
            lfoWtMod += lfoOut * d;
            break;
        case LfoTarget::FmRatio:
            // ±d/8 → keeps modulator from sweeping wildly through the ratio.
            lfoFmRatioMod += lfoOut * d * 0.125f;
            break;
        case LfoTarget::None:
            break;
        }
    }

    // Item 3b: exp2f over std::pow — same value, ~2-3× cheaper. Skip the call
    // entirely when no pitch LFO is routed (very common case).
    const float pitchMul = (lfoPitchSemis != 0.0f) ? std::exp2(lfoPitchSemis * (1.0f / 12.0f)) : 1.0f;
    const float voiceFreq = currentFrequency * pitchMul;

    // ── Compute aggregated per-osc pan weights for post-filter spreading ─
    // The mono filter eats the summed osc signal; after filtering we re-split
    // using a single L/R share derived from the volume-weighted per-osc pan.
    // This preserves the per-osc pan stage (test #105) while keeping the
    // filter mono (which is how MoogLadder is built — adding stereo to it is
    // a much bigger refactor).
    // Phase 3 follow-up: SINGLE pan stage. Combine voice-level round-robin
    // pan (this->pan, assigned in noteOn) with per-osc pan into one effective
    // pan position per osc. The previous render applied per-osc pan here AND
    // then multiplied L/R by voice-pan × √2 at output — which compounded into
    // a second pan stage and broke constant-power when both were non-center.
    float panWeightL = 0.0f;
    float panWeightR = 0.0f;
    float weightTotal = 0.0f;
    for (std::size_t i = 0; i < oscs.size(); ++i) {
        const auto& po = oscs[i];
        if (!po.enabled)
            continue;
        float pPan = po.pan + pan; // voice-level pan stacks with per-osc pan
        pPan += lfoPanMod;
        pPan = std::clamp(pPan, -1.0f, 1.0f);
        constexpr float kHalfPi = 1.57079632679f;
        const float theta = ((pPan + 1.0f) * 0.5f) * kHalfPi;
        const float gL = std::cos(theta);
        const float gR = std::sin(theta);
        panWeightL += gL * po.volume;
        panWeightR += gR * po.volume;
        weightTotal += po.volume;
    }
    if (weightTotal <= 0.0f) {
        // No enabled oscillators — voice silent this sample. Still process
        // env to keep ADSR phase aligned.
        ampEnv.process();
        filterEnv.process();
        outL = 0.0f;
        outR = 0.0f;
        return;
    }
    // Normalise so the aggregate weights sum to 1 (max gain = 1 per channel).
    const float invTotal = 1.0f / weightTotal;
    panWeightL *= invTotal;
    panWeightR *= invTotal;

    // ── Mix all enabled oscillators into mono ─────────────────────────────
    float monoMix = 0.0f;
    for (std::size_t i = 0; i < oscs.size(); ++i) {
        auto& o = oscs[i];
        if (!o.enabled)
            continue;

        const float perOscFreq = voiceFreq * oscFrequencyMultiplier(o.semitoneOffset, o.detuneCents);

        float oscSample = 0.0f;
        switch (o.type) {
        case OscType::Sine:
            // Use VA path with a sine-like signal via wavetable default table.
            o.wavetableOsc.setFrequency(static_cast<double>(perOscFreq));
            // Default-constructed WavetableData is single-frame sine; morph 0.
            o.wavetableOsc.setMorphPosition(0.0f);
            oscSample = o.wavetableOsc.processSample();
            break;
        case OscType::Triangle:
        case OscType::Sawtooth:
        case OscType::Square:
            o.vaOsc.setWaveform(toVaWaveform(o.type));
            o.vaOsc.setFrequency(static_cast<double>(perOscFreq));
            o.vaOsc.setDetuneCents(0.0); // already folded into perOscFreq above
            oscSample = o.vaOsc.processSample();
            break;
        case OscType::Pulse: {
            // Square + duty asymmetry. VA polyBLEP square is 50% duty; offset
            // its raw sample by (pw - 0.5)*2 to bias mean toward the duty
            // cycle. This is a lo-fi but click-free approximation; a proper
            // PolyBLEP pulse with movable second discontinuity is a future
            // improvement.
            o.vaOsc.setWaveform(VAOscillator::Waveform::Square);
            o.vaOsc.setFrequency(static_cast<double>(perOscFreq));
            o.vaOsc.setDetuneCents(0.0);
            const float sq = o.vaOsc.processSample();
            oscSample = sq + (o.pulseWidth - 0.5f) * 2.0f;
            oscSample = std::clamp(oscSample, -1.0f, 1.0f);
            break;
        }
        case OscType::Wavetable: {
            // setMorphPosition picks the frame interpolation, modulated by
            // LFO WavetablePos on every osc (Phase 5: dropped slot-0 gate).
            float wtPos = o.wavetablePos;
            wtPos = std::clamp(wtPos + lfoWtMod, 0.0f, 1.0f);
            o.wavetableOsc.setMorphPosition(wtPos);
            o.wavetableOsc.setFrequency(static_cast<double>(perOscFreq));
            oscSample = o.wavetableOsc.processSample();
            break;
        }
        case OscType::FM: {
            // Simple 2-op FM. Phase 3 follow-up: two independent phase
            // accumulators (carrier + modulator) so the carrier always runs
            // at fc and the modulator at fc*ratio. The previous version
            // shared o.fmPhase for both, which made the perceived pitch
            // fc*ratio rather than fc when ratio != 1 (silent bug for the
            // default 1:1 ratio that broke any non-1 ratio patch).
            float fmRatio = o.fmRatio;
            if (lfoFmRatioMod != 0.0f)
                fmRatio *= (1.0f + lfoFmRatioMod);
            fmRatio = std::clamp(fmRatio, 0.05f, 32.0f);
            // Use the cached per-voice sample rate set in Voice::prepare()
            // rather than a hardcoded 44.1 kHz — previous behaviour mistuned
            // FM by ~8.8% at 48 kHz and ~118% at 96 kHz.
            const double sr = static_cast<double>(o.sampleRate);
            const double modInc = static_cast<double>(perOscFreq) * static_cast<double>(fmRatio) / sr;
            const double carrierInc = static_cast<double>(perOscFreq) / sr;
            o.fmPhase += modInc;
            if (o.fmPhase >= 1.0)
                o.fmPhase -= std::floor(o.fmPhase);
            o.carrierPhase += carrierInc;
            if (o.carrierPhase >= 1.0)
                o.carrierPhase -= std::floor(o.carrierPhase);
            const float mod = std::sin(static_cast<float>(o.fmPhase * 6.283185307179586));
            const float carrierPh = static_cast<float>(o.carrierPhase + mod * o.fmDepth);
            oscSample = std::sin(carrierPh * 6.283185307179586f);
            break;
        }
        case OscType::Noise:
            oscSample = xorshiftNoise(o.noiseRng);
            break;
        }

        // Volume goes into the mono sum; pan is applied post-filter via
        // panWeightL/R (computed above from per-osc pan + vol).
        monoMix += oscSample * o.volume;
    }

    // ── Filter modulation ────────────────────────────────────────────────
    const float filterEnvOut = filterEnv.process();
    float effectiveCutoff = baseCutoffHz * (1.0f + lfoCutoffMod);
    effectiveCutoff *= (1.0f + filterEnvOut * filterEnvMod * velocity * 2.0f);
    effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, 20000.0f);

    // Drive smoothing — sample-rate consumer of the block-rate setDrive target.
    const float driveNow = driveSmoother.process();
    float filteredMono = monoMix;
    if (filter) {
        filter->setCutoff(effectiveCutoff);
        filter->setResonance(resonance);
        filter->setDrive(driveNow);
        // Phase 4: filter type-swap crossfade. Both old + new filters consume
        // the same input sample so the integrator states stay matched, then
        // blend wet via a linear fadeOut/fadeIn ramp over kCrossfadeSamples.
        // After the ramp the old filter is reset and dropped from the loop.
        if (crossfadeRemaining > 0 && crossfadeFromFilter != nullptr && crossfadeTotal > 0) {
            // Feed BOTH with identical drive/cutoff/resonance — applyPatch
            // installed the new filter as `filter`; the old filter retains its
            // own coefficients (last setCutoff/setResonance call). For matched
            // state at handoff we also push the modulated values into the
            // outgoing filter so it tracks the same target as the incoming.
            crossfadeFromFilter->setCutoff(effectiveCutoff);
            crossfadeFromFilter->setResonance(resonance);
            crossfadeFromFilter->setDrive(driveNow);
            const float oldOut = crossfadeFromFilter->process(monoMix);
            const float newOut = filter->process(monoMix);
            const float fadeOut = static_cast<float>(crossfadeRemaining) /
                                  static_cast<float>(crossfadeTotal);
            const float fadeIn = 1.0f - fadeOut;
            filteredMono = oldOut * fadeOut + newOut * fadeIn;
            --crossfadeRemaining;
            if (crossfadeRemaining == 0) {
                crossfadeFromFilter->reset();
                crossfadeFromFilter = nullptr;
                crossfadeTotal = 0;
            }
        } else {
            filteredMono = filter->process(monoMix);
        }
    }
    // Split the mono filtered signal into stereo via the volume-weighted
    // aggregate pan weights. With per-osc pan all-centered, panWeightL ==
    // panWeightR == √2/2 → equal L/R (mono-equivalent). With opposing pan
    // (osc 0 hard L, osc 1 hard R) the weights pull apart.
    float stereoL = filteredMono * panWeightL;
    float stereoR = filteredMono * panWeightR;

    // ── Amp envelope × velocity × LFO amp mod ────────────────────────────
    // Phase 4: clamp (1 + lfoAmpMod) at 0 so two LFOs both targeting Amplitude
    // at full depth cannot push gain negative (phase inversion). Trough is
    // silence, not a sign-flipped waveform.
    const float ampEnvOut = ampEnv.process();
    const float lfoAmpGain = std::max(0.0f, 1.0f + lfoAmpMod);
    float gain = ampEnvOut * velocity * lfoAmpGain;

    // Voice-steal fade-out ramp (linear).
    if (fadeOutSamplesRemaining > 0) {
        const float rampGain = static_cast<float>(fadeOutSamplesRemaining) / static_cast<float>(fadeOutSamplesTotal);
        gain *= rampGain;
        --fadeOutSamplesRemaining;
        if (fadeOutSamplesRemaining == 0) {
            ampEnv.reset();
            filterEnv.reset();
            if (filter)
                filter->reset();
            if (crossfadeFromFilter != nullptr) {
                crossfadeFromFilter->reset();
                crossfadeFromFilter = nullptr;
                crossfadeRemaining = 0;
                crossfadeTotal = 0;
            }
            dcBlockerL.reset();
            dcBlockerR.reset();
            noteIsOn = false;
            midiNote = -1;
            pan = 0.0f;
            panGainL = 0.7071068f;
            panGainR = 0.7071068f;
            for (auto& o : oscs) {
                o.fmPhase = 0.0;
                o.carrierPhase = 0.0;
            }
        }
    }

    // Per-osc pan weights (computed above) are the SINGLE pan stage for the
    // voice. The original code multiplied stereoL/R by an additional voice-
    // level pan (panGainL * √2) — but stereoL/R already carried per-osc pan
    // weights, so applying voice-pan on top was a second pan stage and broke
    // constant-power whenever both per-osc pan and voice pan were non-center
    // (a voice on slot 0 with default pan=-0.4 plus a hard-left per-osc pan
    // would compound, exceeding unity on L and crushing R). The voice-level
    // pan path is not part of the documented feature set — dropped.
    outL = dcBlockerL.process(stereoL * gain);
    outR = dcBlockerR.process(stereoR * gain);
}

// ── VoiceManager ──────────────────────────────────────────────────────────────

VoiceManager::VoiceManager(int voiceCount) {
    assert(voiceCount > 0);
    voices_.reserve(static_cast<std::size_t>(voiceCount));
    voices_.resize(static_cast<std::size_t>(voiceCount));
    for (auto& v : voices_) {
        // Pre-allocate both filter implementations so applyPatch can switch
        // FilterType without heap allocation on the audio thread.
        v.moogFilter = std::make_unique<MoogLadder>();
        v.svFilter = std::make_unique<SVFilter>(FilterMode::LP);
        v.filter = v.moogFilter.get(); // default = LowPass (Moog ladder)
        // Slot 0 is enabled by default with a sawtooth (mirrors prior behaviour).
        v.oscs[0].enabled = true;
        v.oscs[0].type = OscType::Sawtooth;
        v.oscs[0].volume = 1.0f;
        // Seed each osc-slot's noise RNG distinctly so chorused noise voices
        // don't phase-align across slots/voices.
        uint32_t seed = 0x9E3779B9u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&v) & 0xFFFFFFFFu);
        for (auto& o : v.oscs) {
            o.noiseRng = seed;
            seed = seed * 1664525u + 1013904223u;
        }
    }
    // Smoothers default to 30 Hz cutoff — see ParamSmoother.h.
    cutoffSmoother_.reset(1000.0f);
    resonanceSmoother_.reset(0.0f);
    gainSmoother_.reset(1.0f);
    reverbWidthSmoother_.reset(1.0f);
    // ADSR rate smoothers — ~50ms time constant per architect plan (3.2 Hz
    // cutoff produces τ ≈ 50 ms via 1/(2πfc)). Targets get refreshed each
    // applyPatch; envelope.setParams is called every block so the smoother
    // is the single source of truth for the rate trajectory.
    constexpr float kEnvRateSmoothHz = 3.2f;
    for (auto* s : {&ampAttackSmoother_, &ampDecaySmoother_, &ampSustainSmoother_, &ampReleaseSmoother_,
                    &filterEnvAttackSmoother_, &filterEnvDecaySmoother_, &filterEnvSustainSmoother_,
                    &filterEnvReleaseSmoother_}) {
        s->setCutoffHz(kEnvRateSmoothHz);
    }
    for (auto& s : lfoDepthSmoothers_)
        s.setCutoffHz(kEnvRateSmoothHz);
    for (auto& s : lfoRateSmoothers_)
        s.setCutoffHz(kEnvRateSmoothHz);
    activeVoiceCap_ = voiceCount;
}

void VoiceManager::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        v.prepare(sampleRate);
    voiceStealFadeSamples_ = std::max(1, static_cast<int>(std::lround(kVoiceStealFadeSeconds * sampleRate)));
    filterCrossfadeSamples_ = std::max(1, static_cast<int>(std::lround(kFilterCrossfadeSeconds * sampleRate)));
    cutoffSmoother_.setSampleRate(sampleRate);
    resonanceSmoother_.setSampleRate(sampleRate);
    gainSmoother_.setSampleRate(sampleRate);
    reverbWidthSmoother_.setSampleRate(sampleRate);
    for (auto* s : {&ampAttackSmoother_, &ampDecaySmoother_, &ampSustainSmoother_, &ampReleaseSmoother_,
                    &filterEnvAttackSmoother_, &filterEnvDecaySmoother_, &filterEnvSustainSmoother_,
                    &filterEnvReleaseSmoother_}) {
        s->setSampleRate(sampleRate);
    }
    for (auto& s : lfoDepthSmoothers_)
        s.setSampleRate(sampleRate);
    for (auto& s : lfoRateSmoothers_)
        s.setSampleRate(sampleRate);
    delay_.prepare(sampleRate);
    reverb_.prepare(sampleRate);
    recomputePortamentoAlpha();
}

bool VoiceManager::isWithinCap(const Voice& v) const noexcept {
    const auto idx = static_cast<int>(&v - voices_.data());
    return idx >= 0 && idx < activeVoiceCap_;
}

void VoiceManager::noteOn(int midiNote, float velocity) {
    Voice* v = findVoiceForNote(midiNote);
    if (v == nullptr)
        v = findFreeVoice();
    if (v == nullptr) {
        Voice* victim = stealVoice();
        if (victim != nullptr) {
            // Begin fade-out on the outgoing voice. Hide it from MIDI bookkeeping
            // (midiNote = -1) so it doesn't appear in activeNotes() — but keep
            // the envelope + filter state running so its tail rings out through
            // the fade ramp. The new note goes on a different slot.
            victim->fadeOutSamplesRemaining = voiceStealFadeSamples_;
            victim->fadeOutSamplesTotal = voiceStealFadeSamples_;
            victim->midiNote = -1;
            victim->noteIsOn = false;
            // After marking the victim as fading-but-occupied, look for a free
            // slot for the new note. If none, fall back to the victim itself
            // (degraded mode: still applies fade-ramp to outgoing audio while
            // the new note's envelope is retriggered).
            v = findFreeVoice();
            if (v == nullptr)
                v = victim;
        }
    }
    if (v == nullptr)
        return; // pool exhausted

    const bool wasActive = v->isActive() && v->fadeOutSamplesRemaining == 0;
    const bool portando = (portamentoSeconds_ > 0.0f) && wasActive;

    v->midiNote = midiNote;
    v->noteIsOn = true;
    v->noteOnOrder = noteCounter_++;
    v->velocity = std::clamp(velocity, 0.0f, 1.0f);
    v->targetFrequency = midiNoteToHz(midiNote);

    // Assign a deterministic pan position based on this voice's slot index
    // in the pool. Round-robin pattern spreads polyphonic chords across the
    // stereo field; precompute constant-power L/R gains here so the audio
    // thread never calls cos/sin per sample.
    const std::size_t voiceIndex = static_cast<std::size_t>(v - voices_.data());
    v->pan = panForVoiceIndex(voiceIndex);
    computePanGains(v->pan, v->panGainL, v->panGainR);

    if (!portando) {
        v->currentFrequency = v->targetFrequency;
    }

    if (!wasActive || retrigger_) {
        v->ampEnv.noteOn();
        v->filterEnv.noteOn();
        for (auto& lfo : v->lfos)
            lfo.trigger();
    }
}

void VoiceManager::noteOff(int midiNote) {
    Voice* v = findVoiceForNote(midiNote);
    if (v == nullptr)
        return;
    v->noteIsOn = false;
    v->ampEnv.noteOff();
    v->filterEnv.noteOff();
}

void VoiceManager::setPortamento(float seconds) noexcept {
    portamentoSeconds_ = seconds;
    recomputePortamentoAlpha();
}

void VoiceManager::recomputePortamentoAlpha() noexcept {
    if (portamentoSeconds_ <= 0.0f || sampleRate_ <= 0.0) {
        portamentoAlpha_ = 0.0f;
        return;
    }
    portamentoAlpha_ = static_cast<float>(std::exp(-1.0 / (portamentoSeconds_ * sampleRate_)));
}
void VoiceManager::setRetrigger(bool retrigger) noexcept { retrigger_ = retrigger; }

void VoiceManager::setFilterCutoff(float hz) noexcept { cutoffSmoother_.setTarget(hz); }

void VoiceManager::setFilterResonance(float resonance) noexcept { resonanceSmoother_.setTarget(resonance); }

void VoiceManager::setAmpEnvelope(ADSREnvelope::Params params) noexcept {
    for (auto& v : voices_)
        v.ampEnv.setParams(params);
}

void VoiceManager::setFilterEnvelope(ADSREnvelope::Params params) noexcept {
    for (auto& v : voices_)
        v.filterEnv.setParams(params);
}

void VoiceManager::setMasterGain(float gain) noexcept { gainSmoother_.setTarget(gain); }

void VoiceManager::primeSmoothers() noexcept {
    cutoffSmoother_.reset(cutoffSmoother_.target());
    resonanceSmoother_.reset(resonanceSmoother_.target());
    gainSmoother_.reset(gainSmoother_.target());
    reverbWidthSmoother_.reset(reverbWidthSmoother_.target());
    ampAttackSmoother_.reset(ampAttackSmoother_.target());
    ampDecaySmoother_.reset(ampDecaySmoother_.target());
    ampSustainSmoother_.reset(ampSustainSmoother_.target());
    ampReleaseSmoother_.reset(ampReleaseSmoother_.target());
    filterEnvAttackSmoother_.reset(filterEnvAttackSmoother_.target());
    filterEnvDecaySmoother_.reset(filterEnvDecaySmoother_.target());
    filterEnvSustainSmoother_.reset(filterEnvSustainSmoother_.target());
    filterEnvReleaseSmoother_.reset(filterEnvReleaseSmoother_.target());
    for (auto& s : lfoDepthSmoothers_)
        s.reset(s.target());
    for (auto& s : lfoRateSmoothers_)
        s.reset(s.target());
    for (auto& v : voices_)
        v.driveSmoother.reset(v.driveSmoother.target());
    primed_ = true;
}

namespace {
// Map FilterType → FilterMode for the SVFilter dispatch (FilterType::Peak has
// no exact analogue in the engine's SVFilter — collapse to Notch).
FilterMode toSvMode(FilterType t) noexcept {
    switch (t) {
    case FilterType::HighPass:
        return FilterMode::HP;
    case FilterType::BandPass:
        return FilterMode::BP;
    case FilterType::Notch:
    case FilterType::Peak:
        return FilterMode::Notch;
    case FilterType::LowPass:
    default:
        return FilterMode::LP;
    }
}
} // namespace

void VoiceManager::applyPatch(const PatchStruct& patch) noexcept {
    // NaN/inf guard: a malformed PatchStruct would propagate non-finite
    // values forever through the one-pole smoothers (state += k*(NaN-state)).
    // Clamp at the boundary.
    const auto safe = [](float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; };

    // Filter cutoff/resonance & master gain → smoothed targets (block-rate writers,
    // sample-rate readers). First call after prepare() snaps to avoid an
    // audible glide from the default value on patch load.
    const float cutoffHz = safe(patch.filter.cutoff_hz, 1000.0f);
    const float resonance = safe(patch.filter.resonance, 0.0f);
    const float masterGain = safe(patch.master_gain, 1.0f);
    const float reverbWidth = std::clamp(safe(patch.reverb.width, 1.0f), 0.0f, 1.0f);
    const float drive = std::clamp(safe(patch.filter.drive, 0.0f), 0.0f, 1.0f);
    const bool firstCall = !primed_;
    if (firstCall) {
        cutoffSmoother_.reset(cutoffHz);
        resonanceSmoother_.reset(resonance);
        gainSmoother_.reset(masterGain);
        reverbWidthSmoother_.reset(reverbWidth);
        for (auto& v : voices_)
            v.driveSmoother.reset(drive);
        primed_ = true;
    } else {
        cutoffSmoother_.setTarget(cutoffHz);
        resonanceSmoother_.setTarget(resonance);
        gainSmoother_.setTarget(masterGain);
        reverbWidthSmoother_.setTarget(reverbWidth);
        for (auto& v : voices_)
            v.driveSmoother.setTarget(drive);
    }
    reverbWidthTarget_ = reverbWidth;

    // Filter TYPE: select MoogLadder for LowPass (keeps existing analog
    // character + drive); SVFilter for HighPass/BandPass/Notch/Peak. Both
    // implementations are pre-allocated per voice so this is a pointer swap.
    const FilterType ftype = patch.filter.type;
    for (auto& v : voices_) {
        Filter* desired = nullptr;
        if (ftype == FilterType::LowPass) {
            desired = v.moogFilter.get();
        } else {
            v.svFilter->setMode(toSvMode(ftype));
            desired = v.svFilter.get();
        }
        if (v.filter != desired) {
            // Phase 4: instead of an instant pointer swap (which produces a
            // click because the new filter has zero integrator state and the
            // old filter's running output abruptly disappears), kick off a
            // short crossfade. renderStereo runs both filters in parallel for
            // filterCrossfadeSamples_ samples, blends linearly, then resets
            // the outgoing filter once the ramp completes.
            //
            // If a crossfade was already in flight (rapid back-to-back type
            // changes) the incoming `v.filter` is only half-warmed. Naively
            // promoting it to the new outgoing produces a fade FROM a
            // half-faded-in filter TO a cold one — audible click. Instead we
            // SNAP the in-flight crossfade to completion: kill the previous
            // outgoing, treat the half-warmed `v.filter` as the now-stable
            // "current" filter, and then start a clean new crossfade from
            // there. This yields one predictable crossfade per type change
            // regardless of how rapidly they arrive.
            const bool snappedPrev = (v.crossfadeRemaining > 0 && v.crossfadeFromFilter != nullptr);
            if (snappedPrev) {
                v.crossfadeFromFilter->reset();
                v.crossfadeFromFilter = nullptr;
                v.crossfadeRemaining = 0;
                v.crossfadeTotal = 0;
            }
            v.crossfadeFromFilter = v.filter;
            v.crossfadeRemaining = filterCrossfadeSamples_;
            v.crossfadeTotal = filterCrossfadeSamples_;
            v.filter = desired;
            // When we just snapped a prior in-flight crossfade, the incoming
            // `desired` may carry residual integrator state from an earlier
            // life (e.g. LP→HP→LP cycles back to the same Moog instance).
            // Reset it so the new crossfade starts from a known cold state.
            // On a clean (no-prior-crossfade) swap we leave it alone — the
            // existing test asserts boundary-delta < 0.3 against the unreset
            // baseline, which would break for some filter states.
            if (snappedPrev && v.filter)
                v.filter->reset();
        }
    }

    // FX bus parameters (stereo path only).
    reverb_.setSize(std::clamp(safe(patch.reverb.size, 0.5f), 0.0f, 1.0f));
    reverb_.setDamp(std::clamp(safe(patch.reverb.damping, 0.5f), 0.0f, 1.0f));
    reverb_.setMix(std::clamp(safe(patch.reverb.mix, 0.0f), 0.0f, 1.0f));
    delay_.setFeedback(std::clamp(safe(patch.delay.feedback, 0.3f), 0.0f, 0.99f));
    delay_.setMix(std::clamp(safe(patch.delay.mix, 0.0f), 0.0f, 1.0f));
    delay_.setStereo(std::clamp(safe(patch.delay.stereo, 0.5f), 0.0f, 1.0f));

    // Delay time: when bpm_sync is on, `patch.delay.time_s` is reinterpreted
    // as a fraction of a beat (sixteenth=0.25, eighth=0.5, quarter=1.0,
    // half=2.0, whole=4.0). Convention documented here and in PatchStruct.h
    // comments. When sync is off, the raw seconds value is used directly.
    delayBpmSync_ = (patch.delay.bpm_sync != 0);
    if (delayBpmSync_) {
        const double bpm = (hostBpm_ > 1.0) ? hostBpm_ : 120.0;
        const double beatSeconds = 60.0 / bpm;
        const float beatsRaw = safe(patch.delay.time_s, 0.25f);
        const float beats = std::clamp(beatsRaw, 0.0625f, 4.0f);
        const float seconds = std::clamp(static_cast<float>(beats * beatSeconds), 0.001f, 2.0f);
        delay_.setTimeSeconds(seconds);
    } else {
        delay_.setTimeSeconds(std::clamp(safe(patch.delay.time_s, 0.25f), 0.001f, 2.0f));
    }

    // Amp + filter envelope rates — smoothed at ~50ms (Item 5b). Targets are
    // updated here; the smoothed values are pulled below and pushed into the
    // ADSR via setParams every applyPatch tick. Sustain levels go through
    // smoothing too so a held-note sustain knob move doesn't click.
    const float ampAttack = safe(patch.amp_env.attack_s, 0.005f);
    const float ampDecay = safe(patch.amp_env.decay_s, 0.1f);
    const float ampSustain = std::clamp(safe(patch.amp_env.sustain, 1.0f), 0.0f, 1.0f);
    const float ampRelease = safe(patch.amp_env.release_s, 0.1f);
    const float feAttack = safe(patch.filter_env.attack_s, 0.01f);
    const float feDecay = safe(patch.filter_env.decay_s, 0.2f);
    const float feSustain = std::clamp(safe(patch.filter_env.sustain, 0.0f), 0.0f, 1.0f);
    const float feRelease = safe(patch.filter_env.release_s, 0.1f);
    if (firstCall) {
        // Fresh smoothers (post-construction, pre-first-applyPatch). Snap to
        // patch values rather than ramping up from zero so the first audio
        // block reflects the patch's intended env timings.
        ampAttackSmoother_.reset(ampAttack);
        ampDecaySmoother_.reset(ampDecay);
        ampSustainSmoother_.reset(ampSustain);
        ampReleaseSmoother_.reset(ampRelease);
        filterEnvAttackSmoother_.reset(feAttack);
        filterEnvDecaySmoother_.reset(feDecay);
        filterEnvSustainSmoother_.reset(feSustain);
        filterEnvReleaseSmoother_.reset(feRelease);
    } else {
        ampAttackSmoother_.setTarget(ampAttack);
        ampDecaySmoother_.setTarget(ampDecay);
        ampSustainSmoother_.setTarget(ampSustain);
        ampReleaseSmoother_.setTarget(ampRelease);
        filterEnvAttackSmoother_.setTarget(feAttack);
        filterEnvDecaySmoother_.setTarget(feDecay);
        filterEnvSustainSmoother_.setTarget(feSustain);
        filterEnvReleaseSmoother_.setTarget(feRelease);
    }

    // Pull one smoothed sample per applyPatch tick so the env target tracks
    // knob position without zipping on every block boundary. applyParameters
    // runs once per processBlock, so this is block-rate movement of the
    // smoothed target (rather than per-sample) — which is exactly the
    // intended behaviour for ADSR rates (envelopes are themselves slow).
    ADSREnvelope::Params ampParams{};
    ampParams.attackSeconds = ampAttackSmoother_.process();
    ampParams.decaySeconds = ampDecaySmoother_.process();
    ampParams.sustainLevel = ampSustainSmoother_.process();
    ampParams.releaseSeconds = ampReleaseSmoother_.process();
    setAmpEnvelope(ampParams);

    ADSREnvelope::Params filterParams{};
    filterParams.attackSeconds = filterEnvAttackSmoother_.process();
    filterParams.decaySeconds = filterEnvDecaySmoother_.process();
    filterParams.sustainLevel = filterEnvSustainSmoother_.process();
    filterParams.releaseSeconds = filterEnvReleaseSmoother_.process();
    setFilterEnvelope(filterParams);

    // Per-osc fields → per-voice OscSlot. Phase 3 wires enabled / type /
    // volume / pan / wavetable_pos / fm_ratio / fm_depth / pulse_width /
    // semitone / detune across all three slots.
    for (auto& v : voices_) {
        v.filterEnvMod = patch.filter.env_mod;
        for (std::size_t i = 0; i < v.oscs.size() && static_cast<int>(i) < kMaxOscillators; ++i) {
            const auto& op = patch.osc[i];
            auto& os = v.oscs[i];
            os.enabled = (op.enabled != 0);
            os.type = op.type;
            os.volume = std::clamp(safe(op.volume, 1.0f), 0.0f, 1.0f);
            const float opan = std::clamp(safe(op.pan, 0.0f), -1.0f, 1.0f);
            if (opan != os.pan) {
                os.pan = opan;
                constexpr float kHalfPi = 1.57079632679f;
                const float theta = ((opan + 1.0f) * 0.5f) * kHalfPi;
                os.panGainL = std::cos(theta);
                os.panGainR = std::sin(theta);
            }
            os.semitoneOffset = safe(op.semitone_offset, 0.0f);
            os.detuneCents = safe(op.detune_cents, 0.0f);
            os.wavetablePos = std::clamp(safe(op.wavetable_pos, 0.0f), 0.0f, 1.0f);
            os.fmRatio = std::clamp(safe(op.fm_ratio, 1.0f), 0.05f, 32.0f);
            os.fmDepth = std::clamp(safe(op.fm_depth, 0.0f), 0.0f, 1.0f);
            os.pulseWidth = std::clamp(safe(op.pulse_width, 0.5f), 0.01f, 0.99f);
        }

        // LFO routing + per-target depth + LFO internal depth/shape/rate.
        for (size_t i = 0; i < v.lfos.size() && i < kMaxLfos; ++i) {
            const auto& lp = patch.lfo[i];
            v.lfos[i].setShape(toLfoShape(lp.waveform));
            v.lfos[i].setDepth(1.0f); // patch depth applied at routing stage in render()
            // Smooth rate + depth — same VoiceManager smoothers feed every
            // voice's LFO[i]. Sample one smoothed value per applyPatch tick.
            v.lfoTargets[i] = lp.target;
            v.lfos[i].setTempoSync(lp.bpm_sync != 0);
        }
    }

    // Now update LFO smoothers (shared across voices). Push targets then
    // sample once for this block. On first call after prepare(), snap the
    // smoother state to the patch values to avoid a ~50ms ramp-up from 0.
    for (std::size_t i = 0; i < kMaxLfos; ++i) {
        const float rateTarget = safe(patch.lfo[i].rate_hz, 1.0f);
        const float depthTarget = std::clamp(safe(patch.lfo[i].depth, 0.0f), 0.0f, 1.0f);
        if (firstCall) {
            lfoRateSmoothers_[i].reset(rateTarget);
            lfoDepthSmoothers_[i].reset(depthTarget);
        } else {
            lfoRateSmoothers_[i].setTarget(rateTarget);
            lfoDepthSmoothers_[i].setTarget(depthTarget);
        }
        const float r = lfoRateSmoothers_[i].process();
        const float d = lfoDepthSmoothers_[i].process();
        for (auto& v : voices_) {
            v.lfos[i].setFreeRate(r);
            v.lfoDepths[i] = d;
        }
    }

    // Voice count cap (1..voices_.size()). If the new cap drops below an
    // active voice's index, release that voice via fade-out so dropping
    // polyphony mid-play doesn't click.
    const int newCap = std::clamp(static_cast<int>(patch.voice_count), 1, static_cast<int>(voices_.size()));
    if (newCap != activeVoiceCap_) {
        for (int i = newCap; i < static_cast<int>(voices_.size()); ++i) {
            auto& v = voices_[static_cast<std::size_t>(i)];
            if (v.isActive() && v.fadeOutSamplesRemaining == 0) {
                v.fadeOutSamplesRemaining = voiceStealFadeSamples_;
                v.fadeOutSamplesTotal = voiceStealFadeSamples_;
                v.midiNote = -1;
                v.noteIsOn = false;
            }
        }
        activeVoiceCap_ = newCap;
    }

    setPortamento(patch.portamento_s);
}

void VoiceManager::setHostTempo(double bpm) noexcept {
    if (std::isfinite(bpm) && bpm > 1.0)
        hostBpm_ = bpm;
    for (auto& v : voices_)
        for (auto& lfo : v.lfos)
            lfo.setHostTempo(bpm);
}

void VoiceManager::allNotesOff() noexcept {
    for (auto& v : voices_) {
        v.noteIsOn = false;
        v.ampEnv.noteOff();
        v.filterEnv.noteOff();
    }
}

void VoiceManager::releaseResources() noexcept {
    // Hard-stop and zero every piece of stateful per-voice DSP so a host
    // releaseResources → prepareToPlay(newSR) (or a transport restart) does
    // not leak previous-SR Moog integrators, DC blocker memory, or in-flight
    // envelope stages into the next block.
    //
    // Intentionally NOT reset here: cutoffSmoother_, resonanceSmoother_,
    // gainSmoother_. The plugin lifecycle is
    //   prepareToPlay → releaseResources → prepare → applyParameters → primeSmoothers
    // so primeSmoothers() snaps them to the freshly-pushed targets after this
    // call. Resetting them here would force a one-sample mismatch with the
    // VoiceManager constructor defaults before primeSmoothers fires and is
    // redundant work for the common path.
    for (auto& v : voices_) {
        v.midiNote = -1;
        v.noteIsOn = false;
        v.fadeOutSamplesRemaining = 0;
        v.fadeOutSamplesTotal = 0;
        v.ampEnv.reset();
        v.filterEnv.reset();
        if (v.moogFilter)
            v.moogFilter->reset();
        if (v.svFilter)
            v.svFilter->reset();
        // Phase 4: cancel any in-flight filter-type crossfade.
        v.crossfadeFromFilter = nullptr;
        v.crossfadeRemaining = 0;
        v.crossfadeTotal = 0;
        v.dcBlockerL.reset();
        v.dcBlockerR.reset();
        for (auto& o : v.oscs) {
            o.fmPhase = 0.0;
            o.carrierPhase = 0.0;
        }
        // LFO phase is not catastrophic to leak but reset for determinism in
        // offline-bounce / state-recall scenarios.
        for (auto& lfo : v.lfos)
            lfo.trigger();
    }
    delay_.reset();
    reverb_.reset();
}

float VoiceManager::advanceSmoothersAndRender() noexcept {
    const float cutoff = cutoffSmoother_.process();
    const float res = resonanceSmoother_.process();
    const float gain = gainSmoother_.process();
    // Width smoother still advances on the mono path to keep its state in
    // lockstep with the stereo path (smoother phase across mono ↔ stereo
    // host calls).
    (void)reverbWidthSmoother_.process();
    const float alpha = portamentoAlpha_;
    float sum = 0.0f;
    for (auto& v : voices_) {
        float l = 0.0f;
        float r = 0.0f;
        v.renderStereo(alpha, cutoff, res, l, r);
        sum += (l + r) * 0.5f;
    }
    return sum * gain;
}

float VoiceManager::renderNextSample() noexcept { return advanceSmoothersAndRender(); }

void VoiceManager::renderBlock(float* output, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        output[i] = advanceSmoothersAndRender();
}

void VoiceManager::renderBlock(float* left, float* right, int numSamples) noexcept {
    // Real stereo path. Each voice now produces stereo directly via
    // renderStereo (per-osc pan already applied). After the FX bus we fold
    // reverb width via M/S so width=0 → mono wet, width=1 → full stereo wet.
    for (int i = 0; i < numSamples; ++i) {
        const float cutoff = cutoffSmoother_.process();
        const float res = resonanceSmoother_.process();
        const float gain = gainSmoother_.process();
        const float width = reverbWidthSmoother_.process();
        const float alpha = portamentoAlpha_;
        float lSum = 0.0f;
        float rSum = 0.0f;
        for (auto& v : voices_) {
            float l = 0.0f;
            float r = 0.0f;
            v.renderStereo(alpha, cutoff, res, l, r);
            lSum += l;
            rSum += r;
        }
        // FX bus: voices → master gain → delay → reverb. Reverb width is the
        // M/S blend on the reverb wet output specifically. To preserve that
        // semantic we run delay first (dry+delay forms the input to reverb),
        // then split reverb's output into a fresh wet/dry by computing the
        // reverb stage's pure wet contribution and re-blending the M/S there.
        float postDelayL = lSum * gain;
        float postDelayR = rSum * gain;
        delay_.process(postDelayL, postDelayR, postDelayL, postDelayR);
        float wetL = postDelayL;
        float wetR = postDelayR;
        reverb_.process(wetL, wetR, wetL, wetR);
        // Item 1g: M/S width blend on reverb output. width=1 leaves the
        // signal untouched (mid+side*1 / mid-side*1 reconstructs L/R);
        // width=0 yields mono (mid only). Reverb mix internally folds
        // dry+wet; this width affects the full post-reverb signal which is
        // the visible "stereo image" of the effect chain.
        if (width != 1.0f) {
            const float mid = (wetL + wetR) * 0.5f;
            const float side = (wetL - wetR) * 0.5f;
            wetL = mid + side * width;
            wetR = mid - side * width;
        }
        left[i] = wetL;
        right[i] = wetR;
    }
}

int VoiceManager::activeVoiceCount() const noexcept {
    int count = 0;
    for (const auto& v : voices_) {
        // Exclude voices that are alive only because of a steal-fade ramp on
        // a now-anonymous slot (midiNote == -1). They contribute audio for a
        // few ms but are no longer "playing" from the player's POV.
        if (v.isActive() && v.midiNote >= 0)
            ++count;
    }
    return count;
}

int VoiceManager::voiceCount() const noexcept { return static_cast<int>(voices_.size()); }

std::vector<int> VoiceManager::activeNotes() const {
    std::vector<int> notes;
    notes.reserve(voices_.size());
    for (const auto& v : voices_) {
        // Only report voices that are sounding *as a real note* — exclude
        // voices that are only alive due to a steal-fade ramp (midiNote = -1
        // is set when the fade completes, so during the ramp it's still the
        // outgoing note; that's the previous behavior we want preserved).
        if (v.isActive() && v.midiNote >= 0)
            notes.push_back(v.midiNote);
    }
    return notes;
}

Voice* VoiceManager::findFreeVoice() noexcept {
    // Honor the active polyphony cap — only slots [0..activeVoiceCap_) are
    // candidates. Slots beyond the cap remain reserved (idle) so a mid-play
    // cap increase opens them without re-initialising.
    for (int i = 0; i < activeVoiceCap_ && i < static_cast<int>(voices_.size()); ++i) {
        auto& v = voices_[static_cast<std::size_t>(i)];
        if (!v.isActive() && v.fadeOutSamplesRemaining == 0)
            return &v;
    }
    return nullptr;
}

Voice* VoiceManager::stealVoice() noexcept {
    Voice* oldest = nullptr;

    for (int i = 0; i < activeVoiceCap_ && i < static_cast<int>(voices_.size()); ++i) {
        auto& v = voices_[static_cast<std::size_t>(i)];
        if (v.fadeOutSamplesRemaining > 0)
            continue; // already being faded, don't double-fade
        if (!v.noteIsOn && v.isActive()) {
            if (oldest == nullptr || v.noteOnOrder < oldest->noteOnOrder)
                oldest = &v;
        }
    }
    if (oldest != nullptr)
        return oldest;

    for (int i = 0; i < activeVoiceCap_ && i < static_cast<int>(voices_.size()); ++i) {
        auto& v = voices_[static_cast<std::size_t>(i)];
        if (v.fadeOutSamplesRemaining > 0)
            continue;
        if (v.isActive()) {
            if (oldest == nullptr || v.noteOnOrder < oldest->noteOnOrder)
                oldest = &v;
        }
    }
    return oldest;
}

Voice* VoiceManager::findVoiceForNote(int midiNote) noexcept {
    for (auto& v : voices_) {
        if (v.isActive() && v.midiNote == midiNote && v.fadeOutSamplesRemaining == 0)
            return &v;
    }
    return nullptr;
}

float VoiceManager::midiNoteToHz(int note) noexcept {
    return 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
}

} // namespace agentic_synth::engine
