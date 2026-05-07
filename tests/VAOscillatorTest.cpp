#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numeric>
#include <vector>

#include "engine/VAOscillator.h"

using agentic_synth::engine::VAOscillator;

static constexpr double kSampleRate = 44100.0;

namespace {

std::vector<float> render(VAOscillator& osc, int n) {
    std::vector<float> buf(n);
    for (auto& s : buf) s = osc.processSample();
    return buf;
}

double mean(const std::vector<float>& v) {
    double sum = 0.0;
    for (float s : v) sum += s;
    return sum / static_cast<double>(v.size());
}

double rms(const std::vector<float>& v) {
    double sum = 0.0;
    for (float s : v) sum += static_cast<double>(s) * s;
    return std::sqrt(sum / static_cast<double>(v.size()));
}

// Samples covering ~100 complete cycles at 440 Hz
int cycleCount(double freq, int cycles) {
    return static_cast<int>(kSampleRate / freq * cycles);
}

} // namespace

// ---------------------------------------------------------------------------
// DC balance — mean over many complete cycles must be near zero
// ---------------------------------------------------------------------------

TEST_CASE("VAOscillator DC balance - Saw") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Saw);
    osc.setFrequency(440.0);

    auto buf = render(osc, cycleCount(440.0, 100));
    REQUIRE(std::abs(mean(buf)) < 0.01);
}

TEST_CASE("VAOscillator DC balance - Square") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Square);
    osc.setFrequency(440.0);

    auto buf = render(osc, cycleCount(440.0, 100));
    REQUIRE(std::abs(mean(buf)) < 0.01);
}

TEST_CASE("VAOscillator DC balance - Triangle") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Triangle);
    osc.setFrequency(440.0);

    auto buf = render(osc, cycleCount(440.0, 100));
    // slightly looser: integrator accumulates small float rounding per cycle
    REQUIRE(std::abs(mean(buf)) < 0.05);
}

// ---------------------------------------------------------------------------
// Aliasing threshold — PolyBLEP output must stay within ±1.5 even at
// high frequencies where a naive oscillator would alias badly
// ---------------------------------------------------------------------------

TEST_CASE("VAOscillator aliasing threshold - Saw at 10 kHz") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Saw);
    osc.setFrequency(10000.0);

    auto buf = render(osc, static_cast<int>(kSampleRate));
    for (float s : buf) REQUIRE(std::abs(s) <= 1.5f);
}

TEST_CASE("VAOscillator aliasing threshold - Square at 10 kHz") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Square);
    osc.setFrequency(10000.0);

    auto buf = render(osc, static_cast<int>(kSampleRate));
    for (float s : buf) REQUIRE(std::abs(s) <= 1.5f);
}

TEST_CASE("VAOscillator aliasing threshold - Triangle at 10 kHz") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Triangle);
    osc.setFrequency(10000.0);

    auto buf = render(osc, static_cast<int>(kSampleRate));
    for (float s : buf) REQUIRE(std::abs(s) <= 1.5f);
}

// ---------------------------------------------------------------------------
// RMS sanity — approximate match to ideal waveform RMS
//   Saw/Triangle: 1/sqrt(3) ≈ 0.5774   Square: 1.0
// ---------------------------------------------------------------------------

TEST_CASE("VAOscillator RMS - Saw") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Saw);
    osc.setFrequency(440.0);

    auto buf = render(osc, cycleCount(440.0, 200));
    double r = rms(buf);
    constexpr double expected = 1.0 / 1.7320508075688772;  // 1/sqrt(3)
    REQUIRE(r > expected * 0.90);
    REQUIRE(r < expected * 1.10);
}

TEST_CASE("VAOscillator RMS - Square") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Square);
    osc.setFrequency(440.0);

    auto buf = render(osc, cycleCount(440.0, 200));
    double r = rms(buf);
    REQUIRE(r > 0.90);
    REQUIRE(r < 1.10);
}

TEST_CASE("VAOscillator RMS - Triangle") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Triangle);
    osc.setFrequency(440.0);

    auto buf = render(osc, cycleCount(440.0, 200));
    double r = rms(buf);
    constexpr double expected = 1.0 / 1.7320508075688772;
    REQUIRE(r > expected * 0.90);
    REQUIRE(r < expected * 1.10);
}

// ---------------------------------------------------------------------------
// Drift — stays within ±5 cents and actually moves over 60 s
// ---------------------------------------------------------------------------

TEST_CASE("VAOscillator drift within ±5 cents over 60 s") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Saw);
    osc.setFrequency(440.0);

    bool driftMoved = false;
    int total = static_cast<int>(kSampleRate * 60.0);
    for (int i = 0; i < total; ++i) {
        (void)osc.processSample();
        double d = osc.getDriftCents();
        REQUIRE(d >= -5.0);
        REQUIRE(d <=  5.0);
        if (std::abs(d) > 0.001) driftMoved = true;
    }
    REQUIRE(driftMoved);
}

// ---------------------------------------------------------------------------
// Detune — configurable offset shifts frequency without crashing
// ---------------------------------------------------------------------------

TEST_CASE("VAOscillator detune in cents") {
    VAOscillator osc;
    osc.prepare(kSampleRate);
    osc.setWaveform(VAOscillator::Waveform::Saw);
    osc.setFrequency(440.0);
    osc.setDetuneCents(100.0);  // one semitone up

    auto buf = render(osc, static_cast<int>(kSampleRate * 0.1));
    for (float s : buf) REQUIRE(std::abs(s) <= 2.0f);
}
