#include "engine/ADSREnvelope.h"
#include "engine/Filter.h"
#include "engine/LFO.h"
#include "engine/PatchStruct.h"
#include "engine/PatchValidator.h"
#include "engine/RealtimeSafety.h"
#include "engine/SPSCQueue.h"
#include "engine/VAOscillator.h"
#include "engine/VoiceManager.h"
#include "engine/WavetableOscillator.h"
#include "mapper/HeuristicParser.h"
#include <catch2/catch_test_macros.hpp>

using namespace agentic_synth::engine;

// Phase 1 acceptance: 20–30 descriptors produce reasonable patches

struct TestDescriptor {
    const char* input;
    float expectedMinCutoff;
    float expectedMaxCutoff;
};

static const TestDescriptor kDescriptors[] = {
    {"warm pad", 100.0f, 800.0f},      {"bright lead", 1000.0f, 5000.0f}, {"dark ambient", 50.0f, 400.0f},
    {"sub bass", 30.0f, 150.0f},       {"aggressive", 2000.0f, 8000.0f},  {"mellow", 100.0f, 800.0f},
    {"harsh", 3000.0f, 10000.0f},      {"soft", 100.0f, 600.0f},          {"brass", 500.0f, 2500.0f},
    {"string-like", 400.0f, 2000.0f},  {"plucked", 600.0f, 3000.0f},      {"buzzy", 1500.0f, 6000.0f},
    {"hollow", 200.0f, 1000.0f},       {"tinny", 3000.0f, 10000.0f},      {"boomy", 50.0f, 300.0f},
    {"nasal", 500.0f, 2000.0f},        {"smooth", 200.0f, 1000.0f},       {"crisp", 2000.0f, 6000.0f},
    {"round", 150.0f, 800.0f},         {"punchy", 300.0f, 1500.0f},       {"glassy", 2000.0f, 8000.0f},
    {"airy", 3000.0f, 12000.0f},       {"thick", 100.0f, 600.0f},         {"growling", 100.0f, 500.0f},
    {"shimmering", 3000.0f, 10000.0f},
};

TEST_CASE("Phase 1: 25 natural-language descriptors produce valid patches", "[phase1][acceptance]") {
    agentsynth::HeuristicParser parser;
    PatchValidator validator;
    int validCount = 0;

    for (const auto& d : kDescriptors) {
        PatchStruct patch = parser.parse(d.input);
        auto result = validator.validate(patch);
        if (result.valid())
            ++validCount;
    }

    REQUIRE(validCount >= 20); // At least 20 of 25 must produce valid patches
}

TEST_CASE("Phase 1: VoiceManager produces audio for basic patches", "[phase1][acceptance]") {
    VoiceManager voices;
    voices.prepare(44100.0);

    PatchStruct patch{};
    patch.filter.cutoff_hz = 500.0f;
    patch.filter.resonance = 0.2f;
    patch.osc[0].volume = 1.0f;

    voices.applyPatch(patch);
    voices.noteOn(60, 100.0f);

    float output[1024];
    for (int i = 0; i < 1024; ++i) {
        output[i] = voices.renderNextSample();
    }

    // Should produce non-zero audio
    float peak = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        peak = std::max(peak, std::abs(output[i]));
    }
    REQUIRE(peak > 0.0f);
    REQUIRE(peak < 2.0f); // No clipping
}

TEST_CASE("Phase 1: Filter processes input without NaN", "[phase1][acceptance]") {
    MoogLadder filter;
    filter.prepare(44100.0);
    filter.setCutoff(500.0f);
    filter.setResonance(0.3f);

    for (int i = 0; i < 1000; ++i) {
        float input = std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f);
        float output = filter.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(std::abs(output) < 10.0f);
    }
}

TEST_CASE("Phase 1: SPSC queue delivers patches without loss", "[phase1][acceptance]") {
    SPSCQueue<PatchStruct, 64> queue;

    PatchStruct sent{};
    sent.filter.cutoff_hz = 1000.0f;
    sent.filter.resonance = 0.5f;

    REQUIRE(queue.push(sent));

    PatchStruct received{};
    REQUIRE(queue.pop(received));
    REQUIRE(received.filter.cutoff_hz == 1000.0f);
    REQUIRE(received.filter.resonance == 0.5f);
}

TEST_CASE("Phase 1: LFO produces cyclic output", "[phase1][acceptance]") {
    LFO lfo;
    lfo.setRate(5.0f);
    lfo.setDepth(1.0f);
    lfo.setShape(LFO::Shape::Sine);
    lfo.prepare(44100.0);

    float minVal = 1.0f, maxVal = -1.0f;
    for (int i = 0; i < 44100; ++i) {
        float v = lfo.renderNextSample();
        minVal = std::min(minVal, v);
        maxVal = std::max(maxVal, v);
    }

    // Should produce both positive and negative values
    REQUIRE(minVal < -0.5f);
    REQUIRE(maxVal > 0.5f);
}
