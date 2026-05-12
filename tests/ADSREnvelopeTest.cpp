#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/ADSREnvelope.h"

using agentic_synth::engine::ADSREnvelope;

static constexpr double kSR = 44100.0;

static ADSREnvelope::Params makeParams(float attack, float decay, float sustain, float release,
                                       float curvature = 0.0001F) {
    ADSREnvelope::Params p;
    p.attackSeconds = attack;
    p.decaySeconds = decay;
    p.sustainLevel = sustain;
    p.releaseSeconds = release;
    p.curvature = curvature;
    return p;
}

TEST_CASE("ADSREnvelope starts idle") {
    ADSREnvelope env(kSR);
    REQUIRE_FALSE(env.isActive());
    REQUIRE(env.process() == 0.0F);
}

TEST_CASE("ADSREnvelope attack reaches 1.0 within expected time") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.01F, 0.0F, 0.0F, 0.0F));
    env.noteOn();
    REQUIRE(env.isActive());

    const int limit = static_cast<int>(0.01 * kSR * 3); // 3× attack time as budget
    bool reached = false;
    for (int i = 0; i < limit; ++i) {
        if (env.process() >= 1.0F) {
            reached = true;
            break;
        }
    }
    REQUIRE(reached);
}

TEST_CASE("ADSREnvelope decay settles at sustain level") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.0F, 0.05F, 0.5F, 0.0F));
    env.noteOn();

    // Skip past attack (instant) then wait through decay + extra
    const int samples = static_cast<int>(0.05 * kSR * 2);
    float last = 0.0F;
    for (int i = 0; i < samples; ++i)
        last = env.process();

    REQUIRE_THAT(last, Catch::Matchers::WithinAbs(0.5F, 0.01F));
}

TEST_CASE("ADSREnvelope release decays to zero") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.0F, 0.0F, 0.6F, 0.05F));
    env.noteOn();
    // Advance to sustain
    for (int i = 0; i < 100; ++i)
        (void)env.process();

    env.noteOff();
    const int relSamples = static_cast<int>(0.05 * kSR * 3);
    float last = 1.0F;
    for (int i = 0; i < relSamples && env.isActive(); ++i)
        last = env.process();

    REQUIRE_THAT(last, Catch::Matchers::WithinAbs(0.0F, 0.001F));
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("ADSREnvelope retrigger during release does not click") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.1F, 0.05F, 0.7F, 0.2F));
    env.noteOn();

    // Advance past attack + decay to sustain
    const int toSustain = static_cast<int>((0.1 + 0.05) * kSR) + 200;
    for (int i = 0; i < toSustain; ++i)
        (void)env.process();

    // Release partway (50 ms into a 200 ms release)
    env.noteOff();
    const int partway = static_cast<int>(0.05 * kSR);
    float levelBefore = 0.0F;
    for (int i = 0; i < partway; ++i)
        levelBefore = env.process();

    // Retrigger: first new sample must not jump hard (no click)
    env.noteOn();
    const float levelAfter = env.process();
    const float jump = std::abs(levelAfter - levelBefore);

    // A jump > 0.05 (~5 % of full scale over one sample) would be audible
    REQUIRE(jump < 0.05F);
    // And envelope must now be rising
    REQUIRE(env.isActive());
}

TEST_CASE("ADSREnvelope curvature shapes the attack curve") {
    auto halfwayLevel = [](float curvature) -> float {
        ADSREnvelope env(kSR);
        env.setParams(makeParams(0.1F, 0.0F, 0.0F, 0.0F, curvature));
        env.noteOn();
        const int half = static_cast<int>(0.1 * kSR / 2);
        float v = 0.0F;
        for (int i = 0; i < half; ++i)
            v = env.process();
        return v;
    };

    const float exponential = halfwayLevel(0.0001F); // sharper curve — reaches higher midway
    const float gentle = halfwayLevel(0.5F);         // gentler curve — reaches lower midway

    REQUIRE(exponential > gentle);
}

TEST_CASE("ADSREnvelope reset returns to idle") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.1F, 0.1F, 0.5F, 0.1F));
    env.noteOn();
    for (int i = 0; i < 100; ++i)
        (void)env.process();
    REQUIRE(env.isActive());

    env.reset();
    REQUIRE_FALSE(env.isActive());
    REQUIRE(env.process() == 0.0F);
}

TEST_CASE("ADSREnvelope release-from-sustain duration matches releaseSeconds") {
    const float releaseSec = 0.1F;
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.0F, 0.0F, 1.0F, releaseSec));
    env.noteOn();
    // Reach steady sustain (attack/decay are zero, so first sample is at 1.0)
    for (int i = 0; i < static_cast<int>(0.1 * kSR); ++i)
        (void)env.process();

    env.noteOff();
    int samples = 0;
    const int budget = static_cast<int>(releaseSec * kSR * 4);
    while (env.isActive() && samples < budget) {
        env.process();
        ++samples;
    }
    const double measured = samples / kSR;
    // Allow ±10%
    REQUIRE(measured >= releaseSec * 0.9);
    REQUIRE(measured <= releaseSec * 1.1);
}

TEST_CASE("ADSREnvelope release-from-mid-attack still takes releaseSeconds") {
    // Bug regression: noteOff during attack used to produce a noticeably
    // shorter release because releaseBase_ was calibrated assuming start=1.0.
    const float attackSec = 1.0F;
    const float releaseSec = 0.2F;
    ADSREnvelope env(kSR);
    env.setParams(makeParams(attackSec, 0.0F, 1.0F, releaseSec));
    env.noteOn();

    // Advance 100 ms — well inside attack
    const int preSamples = static_cast<int>(0.1 * kSR);
    float midLevel = 0.0F;
    for (int i = 0; i < preSamples; ++i)
        midLevel = env.process();
    REQUIRE(midLevel > 0.01F);
    REQUIRE(midLevel < 1.0F);

    env.noteOff();
    int samples = 0;
    const int budget = static_cast<int>(releaseSec * kSR * 4);
    while (env.isActive() && samples < budget) {
        env.process();
        ++samples;
    }
    const double measured = samples / kSR;
    // Must be close to configured releaseSec (not faster — that was the bug).
    REQUIRE(measured >= releaseSec * 0.9);
    REQUIRE(measured <= releaseSec * 1.1);
}

TEST_CASE("ADSREnvelope release-from-zero terminates without UB") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(1.0F, 0.0F, 1.0F, 0.2F));
    env.noteOn();
    // output_ is still 0 on the very first sample before any process() call.
    REQUIRE(env.isActive());
    env.noteOff();
    // Should immediately go idle (start=0 → nothing to release).
    const float v = env.process();
    REQUIRE(v == 0.0F);
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("ADSREnvelope release-from-mid-decay duration matches releaseSeconds") {
    // SDET gap: every previous "release-time matches releaseSeconds" check
    // entered Release from sustain (level 0.5 or 1.0) or from attack. Entering
    // Release in the middle of the DECAY stage exercises a separate path —
    // the start level is somewhere between sustain and 1.0 — and must also
    // honour the configured release time.
    const float releaseSec = 0.2F;
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.001F, 1.0F, 0.2F, releaseSec));
    env.noteOn();

    // Walk forward until we land in the middle of decay — output ≈ 0.6.
    // The 1 s decay from 1.0 to 0.2 means level 0.6 occurs ~midway.
    float lvl = 0.0F;
    int safety = 0;
    while (lvl < 0.6F && safety++ < static_cast<int>(kSR * 2)) {
        lvl = env.process();
    }
    REQUIRE(lvl >= 0.55F);
    REQUIRE(lvl <= 0.75F);

    env.noteOff();
    int samples = 0;
    const int budget = static_cast<int>(releaseSec * kSR * 4);
    while (env.isActive() && samples < budget) {
        env.process();
        ++samples;
    }
    const double measured = samples / kSR;
    INFO("measured release from mid-decay = " << measured << "s (expected " << releaseSec << ")");
    REQUIRE(measured >= releaseSec * 0.9);
    REQUIRE(measured <= releaseSec * 1.1);
}

TEST_CASE("ADSREnvelope release rescale survives setParams (applyPatch-every-block regression)") {
    // CRITICAL regression: VoiceManager::applyPatch runs every audio block
    // and calls setAmpEnvelope/setFilterEnvelope, which forwards to
    // setParams → recalcCoefficients. Before the FIX, recalcCoefficients
    // unconditionally re-derived the release coeffs from start=1.0,
    // clobbering the current-level rescale that noteOff() had installed.
    // Symptom: any held-then-released note got its release trajectory snapped
    // back to a 1.0→0 ramp mid-flight → click + wrong release time.
    const float releaseSec = 0.5F;
    const ADSREnvelope::Params p = makeParams(0.001F, 0.001F, 0.5F, releaseSec);
    ADSREnvelope env(kSR);
    env.setParams(p);
    env.noteOn();

    // Settle into sustain.
    for (int i = 0; i < static_cast<int>(0.05 * kSR); ++i)
        (void)env.process();

    env.noteOff(); // Enter Release at output ≈ sustain (0.5).

    // Process 100 ms — well into release.
    const int preSamples = static_cast<int>(0.1 * kSR);
    float midLevel = 0.0F;
    for (int i = 0; i < preSamples; ++i)
        midLevel = env.process();

    // With a working rescale, after 100 ms (= 1/5 of releaseSec) the
    // exponential should be a fraction of the start level. Just sanity-check
    // it's somewhere reasonable (not 0, not near start).
    INFO("mid-release level = " << midLevel);
    REQUIRE(midLevel > 0.001F);
    REQUIRE(midLevel < 0.5F);

    // Now hammer setParams with the SAME params, simulating applyPatch every
    // block. The FIX guarantees this does NOT reset releaseCoeff_/Base_ to
    // the start=1.0 calibration when stage == Release.
    env.setParams(p);
    env.setParams(p);
    env.setParams(p);

    // Continue processing until idle. Total release time (from noteOff to
    // idle) must still be ≈ releaseSec.
    int samplesAfterMid = 0;
    const int budget = static_cast<int>(releaseSec * kSR * 4);
    while (env.isActive() && samplesAfterMid < budget) {
        env.process();
        ++samplesAfterMid;
    }
    const double total = static_cast<double>(preSamples + samplesAfterMid) / kSR;
    INFO("total measured release = " << total << "s (expected " << releaseSec << ")");
    REQUIRE(total >= releaseSec * 0.9);
    REQUIRE(total <= releaseSec * 1.1);
}

TEST_CASE("ADSREnvelope zero-time attack is sample-accurate") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.0F, 0.0F, 1.0F, 0.0F));
    env.noteOn();
    // First sample must already be at peak
    REQUIRE(env.process() >= 1.0F);
}

TEST_CASE("ADSREnvelope re-noteOff during release is idempotent",
          "[adsr][release][idempotent]") {
    // Phase 5: guard against re-rescaling release trajectory on repeated
    // noteOff during the release stage. Without the guard, the second
    // noteOff re-measures output_ (now lower) and re-rescales the coeff so
    // "remaining decay finishes in releaseSeconds from NOW" — extending the
    // total release duration beyond what was configured.
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.001F, 0.0F, 1.0F, 0.2F)); // 200ms release
    env.noteOn();
    for (int i = 0; i < 200; ++i)
        env.process(); // reach sustain (1.0)

    env.noteOff();
    // Process halfway through release.
    const int halfSamples = static_cast<int>(0.1 * kSR);
    for (int i = 0; i < halfSamples; ++i)
        env.process();

    // Capture output, second noteOff, see if release re-extends.
    const float midRelease = env.process();
    REQUIRE(midRelease > 0.0F);
    REQUIRE(midRelease < 1.0F);

    env.noteOff(); // re-noteOff — should be no-op
    int samplesToZero = 0;
    while (env.isActive() && samplesToZero < static_cast<int>(0.5 * kSR)) {
        env.process();
        ++samplesToZero;
    }
    // Total release should be ~200ms (started 100ms ago, ~100ms remaining).
    // Bound: samplesToZero must be < 0.15s. Without idempotent guard,
    // it would be ~200ms (rescaled from current).
    const double remainingSeconds = static_cast<double>(samplesToZero) / kSR;
    INFO("remaining release after re-noteOff = " << remainingSeconds << "s");
    REQUIRE(remainingSeconds < 0.15);
}
