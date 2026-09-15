#include "engine/LFO.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

using namespace agentic_synth::engine;

TEST_CASE("LFO tempo-sync rate matches host BPM within 0.1%", "[LFO][tempo-sync]") {
    LFO lfo;
    lfo.setSampleRate(44100.0);

    SECTION("120 BPM quarter note = 2 Hz") {
        lfo.setHostTempo(120.0);
        lfo.setTempoSync(true, LfoSyncDivision::Quarter);
        double expected = (120.0 / 60.0) / 1.0;
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("90 BPM eighth note = 3 Hz") {
        lfo.setHostTempo(90.0);
        lfo.setTempoSync(true, LfoSyncDivision::Eighth);
        double expected = (90.0 / 60.0) / 0.5;
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("140 BPM whole note") {
        lfo.setHostTempo(140.0);
        lfo.setTempoSync(true, LfoSyncDivision::Whole);
        double expected = (140.0 / 60.0) / 4.0;
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("120 BPM dotted quarter") {
        lfo.setHostTempo(120.0);
        lfo.setTempoSync(true, LfoSyncDivision::QuarterDotted);
        double expected = (120.0 / 60.0) / 1.5;
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("120 BPM eighth triplet") {
        lfo.setHostTempo(120.0);
        lfo.setTempoSync(true, LfoSyncDivision::EighthTriplet);
        double expected = (120.0 / 60.0) / (1.0 / 3.0);
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("200 BPM sixteenth note") {
        lfo.setHostTempo(200.0);
        lfo.setTempoSync(true, LfoSyncDivision::Sixteenth);
        double expected = (200.0 / 60.0) / 0.25;
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("tempo change updates rate") {
        lfo.setTempoSync(true, LfoSyncDivision::Quarter);
        lfo.setHostTempo(200.0);
        double expected = (200.0 / 60.0) / 1.0;
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(expected, 0.001));
    }

    SECTION("disabling tempo sync reverts to free rate") {
        lfo.setHostTempo(120.0);
        lfo.setTempoSync(true, LfoSyncDivision::Quarter);
        lfo.setFreeRate(5.0f);
        // After disabling tempo sync, rate should be 5 Hz (free rate)
        lfo.setTempoSync(false);
        REQUIRE_THAT(lfo.currentRateHz(), Catch::Matchers::WithinRel(5.0, 0.001));
    }
}

TEST_CASE("LFO phase reset on key trigger", "[LFO][trigger]") {
    LFO lfo;
    lfo.setSampleRate(44100.0);
    lfo.setShape(LfoShape::Sine);
    lfo.setFreeRate(1.0f);
    lfo.setDepth(1.0f);

    SECTION("trigger resets phase — sine output near zero") {
        lfo.setKeyTrigger(true);
        for (int i = 0; i < 1000; ++i)
            lfo.processSample();
        lfo.trigger();
        float val = lfo.processSample();
        // sine at phase≈0 is ≈0
        REQUIRE_THAT(val, Catch::Matchers::WithinAbs(0.0f, 0.01f));
    }

    SECTION("trigger in free-run mode has no effect") {
        lfo.setKeyTrigger(false);
        // advance well past zero crossing
        for (int i = 0; i < 11025; ++i) // quarter cycle at 1 Hz / 44100 Hz
            lfo.processSample();
        lfo.trigger();
        float val = lfo.processSample();
        // should NOT be near zero — we're at ~quarter cycle (sine ≈ 1)
        REQUIRE(std::abs(val) > 0.5f);
    }

    SECTION("reset() always clears phase regardless of key-trigger mode") {
        lfo.setKeyTrigger(false);
        for (int i = 0; i < 1000; ++i)
            lfo.processSample();
        lfo.reset();
        float val = lfo.processSample();
        REQUIRE_THAT(val, Catch::Matchers::WithinAbs(0.0f, 0.01f));
    }
}

TEST_CASE("LFO waveform output range", "[LFO][shapes]") {
    LFO lfo;
    lfo.setSampleRate(44100.0);
    lfo.setFreeRate(1.0f);
    lfo.setDepth(1.0f);

    auto checkRange = [&](float lo, float hi) {
        for (int i = 0; i < 44100; ++i) {
            float v = lfo.processSample();
            REQUIRE(v >= lo);
            REQUIRE(v <= hi);
        }
    };

    SECTION("sine in [-1, 1]") {
        lfo.setShape(LfoShape::Sine);
        lfo.reset();
        checkRange(-1.0f, 1.0f);
    }

    SECTION("triangle in [-1, 1]") {
        lfo.setShape(LfoShape::Triangle);
        lfo.reset();
        checkRange(-1.0f, 1.0f);
    }

    SECTION("saw in [-1, 1]") {
        lfo.setShape(LfoShape::Saw);
        lfo.reset();
        checkRange(-1.0f, 1.0f);
    }

    SECTION("square stays in [-1, 1] and settles near extremes") {
        lfo.setShape(LfoShape::Square);
        lfo.reset();
        checkRange(-1.0f, 1.0f);
        lfo.reset();
        float peak = 0.0f;
        for (int i = 0; i < 44100; ++i)
            peak = std::max(peak, std::abs(lfo.processSample()));
        REQUIRE(peak > 0.99f);
    }

    SECTION("S+H in [-1, 1]") {
        lfo.setShape(LfoShape::SampleAndHold);
        lfo.reset();
        checkRange(-1.0f, 1.0f);
    }
}

TEST_CASE("LFO depth scales output", "[LFO][depth]") {
    LFO lfo;
    lfo.setSampleRate(44100.0);
    lfo.setShape(LfoShape::Sine);
    lfo.setFreeRate(1.0f);

    SECTION("depth 0.5 halves peak amplitude") {
        lfo.setDepth(0.5f);
        lfo.reset();
        float peak = 0.0f;
        for (int i = 0; i < 44100; ++i)
            peak = std::max(peak, std::abs(lfo.processSample()));
        REQUIRE_THAT(peak, Catch::Matchers::WithinRel(0.5f, 0.001f));
    }

    SECTION("depth 0 gives silence") {
        lfo.setDepth(0.0f);
        lfo.reset();
        for (int i = 0; i < 44100; ++i)
            REQUIRE(lfo.processSample() == 0.0f);
    }
}

TEST_CASE("LFO division beats per cycle table", "[LFO][division]") {
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::Whole), Catch::Matchers::WithinRel(4.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::Half), Catch::Matchers::WithinRel(2.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::Quarter), Catch::Matchers::WithinRel(1.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::Eighth), Catch::Matchers::WithinRel(0.5, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::Sixteenth), Catch::Matchers::WithinRel(0.25, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::ThirtySecond), Catch::Matchers::WithinRel(0.125, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::SixtyFourth), Catch::Matchers::WithinRel(0.0625, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::WholeDotted), Catch::Matchers::WithinRel(6.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::HalfDotted), Catch::Matchers::WithinRel(3.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::QuarterDotted), Catch::Matchers::WithinRel(1.5, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::EighthDotted), Catch::Matchers::WithinRel(0.75, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::SixteenthDotted),
                 Catch::Matchers::WithinRel(0.375, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::HalfTriplet),
                 Catch::Matchers::WithinRel(4.0 / 3.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::QuarterTriplet),
                 Catch::Matchers::WithinRel(2.0 / 3.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::EighthTriplet),
                 Catch::Matchers::WithinRel(1.0 / 3.0, 0.001));
    REQUIRE_THAT(LFO::divisionBeatsPerCycle(LfoSyncDivision::SixteenthTriplet),
                 Catch::Matchers::WithinRel(1.0 / 6.0, 0.001));
}

TEST_CASE("LFO target slot routing", "[LFO][routing]") {
    LFO lfo;
    REQUIRE(lfo.targetSlot() == 0);
    lfo.setTargetSlot(3);
    REQUIRE(lfo.targetSlot() == 3);
}

TEST_CASE("LFO output slew", "[LFO][slew]") {
    constexpr double sr = 44100.0;

    auto maxAbsDelta = [](LFO& lfo, int n) {
        float prev = lfo.processSample();
        float maxDelta = 0.0f;
        for (int i = 1; i < n; ++i) {
            float v = lfo.processSample();
            maxDelta = std::max(maxDelta, std::abs(v - prev));
            prev = v;
        }
        return maxDelta;
    };

    SECTION("square max sample-to-sample delta ≪ 2.0") {
        LFO lfo;
        lfo.setSampleRate(sr);
        lfo.setShape(LfoShape::Square);
        lfo.setFreeRate(20.0f);
        lfo.setDepth(1.0f);
        lfo.reset();
        const float maxDelta = maxAbsDelta(lfo, static_cast<int>(sr));
        REQUIRE(maxDelta < 0.1f);
        REQUIRE(maxDelta > 0.001f);
    }

    SECTION("S&H max sample-to-sample delta ≪ 2.0") {
        LFO lfo;
        lfo.seed(7u);
        lfo.setSampleRate(sr);
        lfo.setShape(LfoShape::SampleAndHold);
        lfo.setFreeRate(20.0f);
        lfo.setDepth(1.0f);
        lfo.reset();
        const float maxDelta = maxAbsDelta(lfo, static_cast<int>(sr));
        REQUIRE(maxDelta < 0.1f);
    }

    SECTION("sine highly correlated with ideal at 5 Hz") {
        LFO lfo;
        lfo.setSampleRate(sr);
        lfo.setShape(LfoShape::Sine);
        lfo.setFreeRate(5.0f);
        lfo.setDepth(1.0f);
        lfo.reset();

        // One-pole τ≈1.5 ms adds ~atan(ωτ) lag (~2.7° at 5 Hz); still nearly identical.
        const int n = static_cast<int>(sr);
        constexpr int skip = 1000;
        double sumXY = 0.0;
        double sumX2 = 0.0;
        double sumY2 = 0.0;
        float maxAbsErr = 0.0f;
        for (int i = 0; i < n; ++i) {
            const double ideal = std::sin(2.0 * std::numbers::pi * 5.0 * static_cast<double>(i) / sr);
            const double y = static_cast<double>(lfo.processSample());
            if (i < skip)
                continue;
            sumXY += ideal * y;
            sumX2 += ideal * ideal;
            sumY2 += y * y;
            maxAbsErr = std::max(maxAbsErr, static_cast<float>(std::abs(ideal - y)));
        }
        const double corr = sumXY / std::sqrt(sumX2 * sumY2);
        REQUIRE(corr > 0.998);
        REQUIRE(maxAbsErr < 0.05f);
    }

    SECTION("S&H holds plateaus between jumps") {
        LFO lfo;
        lfo.seed(42u);
        lfo.setSampleRate(sr);
        lfo.setShape(LfoShape::SampleAndHold);
        lfo.setFreeRate(5.0f);
        lfo.setDepth(1.0f);
        lfo.reset();

        const int samplesPerCycle = static_cast<int>(sr / 5.0);
        constexpr int settle = 800; // ~18 ms ≈ 12τ — asymptotic remainder negligible
        for (int cycle = 0; cycle < 5; ++cycle) {
            float prev = lfo.processSample();
            for (int i = 1; i < settle; ++i)
                prev = lfo.processSample();

            float maxPlateauDelta = 0.0f;
            for (int i = settle; i < samplesPerCycle; ++i) {
                const float v = lfo.processSample();
                maxPlateauDelta = std::max(maxPlateauDelta, std::abs(v - prev));
                prev = v;
            }
            REQUIRE(maxPlateauDelta < 1e-4f);
        }
    }

    SECTION("reset and key-trigger clear slew state") {
        LFO lfo;
        lfo.setSampleRate(sr);
        lfo.setShape(LfoShape::Square);
        lfo.setFreeRate(1.0f);
        lfo.setDepth(1.0f);
        lfo.setKeyTrigger(true);
        for (int i = 0; i < 1000; ++i)
            lfo.processSample();

        lfo.reset();
        // After clear, first sample is coeff * target(+1), not a lingering plateau.
        REQUIRE(std::abs(lfo.processSample()) < 0.05f);

        for (int i = 0; i < 1000; ++i)
            lfo.processSample();
        lfo.trigger();
        REQUIRE(std::abs(lfo.processSample()) < 0.05f);
    }
}
