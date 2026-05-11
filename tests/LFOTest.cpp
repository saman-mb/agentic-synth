#include "engine/LFO.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

    SECTION("square is exactly +1 or -1") {
        lfo.setShape(LfoShape::Square);
        lfo.reset();
        for (int i = 0; i < 44100; ++i) {
            float v = lfo.processSample();
            REQUIRE((v == 1.0f || v == -1.0f));
        }
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
