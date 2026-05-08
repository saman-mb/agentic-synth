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

    REQUIRE(last == 0.0F);
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

TEST_CASE("ADSREnvelope zero-time attack is sample-accurate") {
    ADSREnvelope env(kSR);
    env.setParams(makeParams(0.0F, 0.0F, 1.0F, 0.0F));
    env.noteOn();
    // First sample must already be at peak
    REQUIRE(env.process() >= 1.0F);
}
