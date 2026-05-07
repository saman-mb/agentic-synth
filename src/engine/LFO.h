#pragma once
#include <random>

namespace agentic_synth::engine {

enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold };

enum class LfoSyncDivision {
    Whole,
    Half,
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    SixtyFourth,
    WholeDotted,
    HalfDotted,
    QuarterDotted,
    EighthDotted,
    SixteenthDotted,
    HalfTriplet,
    QuarterTriplet,
    EighthTriplet,
    SixteenthTriplet
};

class LFO {
public:
    LFO();

    void setSampleRate(double sampleRate);
    void setShape(LfoShape shape);
    void setDepth(float depth);

    void setFreeRate(float hz);

    void setTempoSync(bool enabled, LfoSyncDivision division = LfoSyncDivision::Quarter);
    void setHostTempo(double bpm);

    void setKeyTrigger(bool enabled);
    void trigger();

    float processSample();
    void reset();

    void setTargetSlot(int slot);
    [[nodiscard]] int targetSlot() const;

    [[nodiscard]] double currentRateHz() const;
    [[nodiscard]] static double divisionBeatsPerCycle(LfoSyncDivision division);

private:
    double mSampleRate{44100.0};
    LfoShape mShape{LfoShape::Sine};
    float mDepth{1.0f};
    float mFreeRate{1.0f};
    bool mTempoSync{false};
    LfoSyncDivision mSyncDivision{LfoSyncDivision::Quarter};
    double mHostBpm{120.0};
    bool mKeyTrigger{false};
    double mPhase{0.0};
    double mPrevPhase{1.0};
    int mTargetSlot{0};

    float mHeldValue{0.0f};
    std::mt19937 mRng;
    std::uniform_real_distribution<float> mDist{-1.0f, 1.0f};

    [[nodiscard]] float computeShape(double phase);
};

} // namespace agentic_synth::engine
