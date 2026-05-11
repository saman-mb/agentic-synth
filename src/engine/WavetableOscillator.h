#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace agentic_synth::engine {

// Test-only entry point for the offline radix-2 FFT used to build mip
// pyramids. Power-of-two sizes only. `inverse=false` performs the forward
// transform; `inverse=true` performs the inverse and divides by N.
// Not for audio-thread use — allocates if `a.size()` is changed by caller.
void fftRadix2ForTesting(std::vector<std::complex<float>>& a, bool inverse);

inline constexpr int kWavetableSize = 256;
inline constexpr int kMaxWavetableFrames = 256;
// 7 mip levels: cutoff bins 128, 64, 32, 16, 8, 4, 2 — the lowest still
// preserves the fundamental sine (bin 1). An 8th level would land at cutoff=1
// (DC only) → silent. The phaseIncrement clamp in mipLevelFloat() already
// guarantees we never request a level above (kNumMipLevels-1), so 7 covers
// every musically reachable phaseIncrement < 0.5.
inline constexpr int kNumMipLevels = 7;

// Band-limited mip pyramid: all mips are kWavetableSize samples.
// Mip level k zeroes DFT bins above kWavetableSize / 2^(k+1), so its harmonic
// content stays below Nyquist when played at frequencies up to sampleRate / 2^(k+1).
struct WavetableData {
    // mips[level][frame] holds kWavetableSize band-limited samples for that level.
    std::array<std::array<std::vector<float>, kMaxWavetableFrames>, kNumMipLevels> mips{};
    int frameCount{0};

    // Populate from a flat buffer of numFrames * kWavetableSize samples (row-major).
    // Builds all mip levels via FFT bin-zeroing (offline; allocates).
    void buildFromFrames(const float* samples, int numFrames);

private:
    // Bandlimit the source-frame (mip 0) in-place into level k.
    void buildMipLevel(int level);
};

class WavetableOscillator {
public:
    // Default-constructs with a single-frame sine wave table.
    WavetableOscillator();

    // Load frames from a PCM16 or IEEE-float32 WAV file.
    // Each kWavetableSize consecutive samples form one frame.
    // Returns false on parse error.
    bool loadFromWav(const std::string& path);

    // Load directly from caller-owned memory.
    void loadFromFrames(const float* samples, int numFrames);

    // Hot-swap the active table (call from the audio thread, or synchronise externally).
    void setTable(std::shared_ptr<const WavetableData> table);

    void setSampleRate(double sampleRate) noexcept;
    void setFrequency(double hz) noexcept;
    void setMorphPosition(float position) noexcept; // 0..1 across frames

    void reset() noexcept;

    [[nodiscard]] float processSample() noexcept;
    void processBlock(float* output, int numSamples) noexcept;

    // Test accessors / knobs (no DSP cost in release path).
    void setMipCrossfadeEnabled(bool enabled) noexcept { mipCrossfadeEnabled_ = enabled; }
    [[nodiscard]] float currentMipLevelF() const noexcept;

private:
    [[nodiscard]] float mipLevelFloat() const noexcept;
    [[nodiscard]] float readMorphedSample(int mipLevel, double phase) const noexcept;

    std::shared_ptr<const WavetableData> table_;
    double sampleRate_{44100.0};
    double frequency_{440.0};
    double phaseIncrement_{440.0 / 44100.0};
    double phase_{0.0}; // 0..1
    float morphPos_{0.0f};
    bool mipCrossfadeEnabled_{true};
};

} // namespace agentic_synth::engine
