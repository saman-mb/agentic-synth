#include "LFO.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace agentic_synth::engine {

LFO::LFO() : mRng(std::random_device{}()) {}

void LFO::setSampleRate(double sampleRate) { mSampleRate = sampleRate; }

void LFO::setShape(LfoShape shape) { mShape = shape; }

void LFO::setDepth(float depth) { mDepth = std::clamp(depth, 0.0f, 1.0f); }

void LFO::setFreeRate(float hz) { mFreeRate = hz; }

void LFO::setTempoSync(bool enabled, LfoSyncDivision division) {
    mTempoSync = enabled;
    mSyncDivision = division;
}

void LFO::setHostTempo(double bpm) { mHostBpm = bpm; }

void LFO::setKeyTrigger(bool enabled) { mKeyTrigger = enabled; }

void LFO::trigger() {
    if (mKeyTrigger) {
        mPhase = 0.0;
        mPrevPhase = 1.0;
    }
}

float LFO::processSample() {
    float out = computeShape(mPhase) * mDepth;
    mPrevPhase = mPhase;
    mPhase += currentRateHz() / mSampleRate;
    if (mPhase >= 1.0)
        mPhase -= std::floor(mPhase);
    return out;
}

void LFO::reset() {
    mPhase = 0.0;
    mPrevPhase = 1.0;
}

void LFO::setTargetSlot(int slot) { mTargetSlot = slot; }

int LFO::targetSlot() const { return mTargetSlot; }

double LFO::currentRateHz() const {
    if (mTempoSync)
        return (mHostBpm / 60.0) / divisionBeatsPerCycle(mSyncDivision);
    return mFreeRate;
}

double LFO::divisionBeatsPerCycle(LfoSyncDivision division) {
    switch (division) {
    case LfoSyncDivision::Whole:
        return 4.0;
    case LfoSyncDivision::Half:
        return 2.0;
    case LfoSyncDivision::Quarter:
        return 1.0;
    case LfoSyncDivision::Eighth:
        return 0.5;
    case LfoSyncDivision::Sixteenth:
        return 0.25;
    case LfoSyncDivision::ThirtySecond:
        return 0.125;
    case LfoSyncDivision::SixtyFourth:
        return 0.0625;
    case LfoSyncDivision::WholeDotted:
        return 6.0;
    case LfoSyncDivision::HalfDotted:
        return 3.0;
    case LfoSyncDivision::QuarterDotted:
        return 1.5;
    case LfoSyncDivision::EighthDotted:
        return 0.75;
    case LfoSyncDivision::SixteenthDotted:
        return 0.375;
    case LfoSyncDivision::HalfTriplet:
        return 4.0 / 3.0;
    case LfoSyncDivision::QuarterTriplet:
        return 2.0 / 3.0;
    case LfoSyncDivision::EighthTriplet:
        return 1.0 / 3.0;
    case LfoSyncDivision::SixteenthTriplet:
        return 1.0 / 6.0;
    }
    return 1.0;
}

float LFO::computeShape(double phase) {
    switch (mShape) {
    case LfoShape::Sine:
        return static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));

    case LfoShape::Triangle:
        if (phase < 0.25)
            return static_cast<float>(4.0 * phase);
        if (phase < 0.75)
            return static_cast<float>(2.0 - 4.0 * phase);
        return static_cast<float>(4.0 * phase - 4.0);

    case LfoShape::Saw:
        return static_cast<float>(2.0 * phase - 1.0);

    case LfoShape::Square:
        return phase < 0.5 ? 1.0f : -1.0f;

    case LfoShape::SampleAndHold:
        if (mPrevPhase > phase) // phase wrapped — new cycle
            mHeldValue = mDist(mRng);
        return mHeldValue;
    }
    return 0.0f;
}

} // namespace agentic_synth::engine
