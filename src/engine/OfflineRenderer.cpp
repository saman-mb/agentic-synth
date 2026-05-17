#include "engine/OfflineRenderer.h"

#include "engine/VoiceManager.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace agentic_synth::engine {

namespace {

constexpr int kRenderBlockSize = 512;

// Velocity 1..127 → 0..1 used by VoiceManager::noteOn.
inline float velocityToFloat(int v) noexcept {
    const float clamped = static_cast<float>(std::clamp(v, 1, 127));
    return clamped / 127.0f;
}

} // namespace

std::vector<float> renderPatchToBuffer(const PatchStruct& patch, const BounceConfig& cfg) {
    const int sampleRate = std::max(8000, cfg.sample_rate_hz);
    const int channels = std::clamp(cfg.channels, 1, 2);
    const float durationS = std::max(0.05f, cfg.duration_s);
    const float holdS = std::clamp(cfg.note_hold_s, 0.0f, durationS);

    const int totalFrames = static_cast<int>(std::lround(durationS * static_cast<double>(sampleRate)));
    const int holdFrames = static_cast<int>(std::lround(holdS * static_cast<double>(sampleRate)));

    std::vector<float> buffer;
    buffer.assign(static_cast<size_t>(totalFrames) * static_cast<size_t>(channels), 0.0f);

    // Build a fresh VoiceManager — never touches the live audio thread's instance.
    VoiceManager vm{VoiceManager::kDefaultVoiceCount};
    vm.prepare(static_cast<double>(sampleRate));
    vm.applyPatch(patch);
    vm.primeSmoothers();

    // Kick the note. Inject patch a second time so the smoothers settle past
    // any residual init state before voice rendering begins.
    vm.applyPatch(patch);
    vm.noteOn(std::clamp(cfg.midi_note, 0, 127), velocityToFloat(cfg.velocity));

    // Scratch stereo block on the stack (≤512×2 floats = 4KB).
    std::vector<float> scratchL(static_cast<size_t>(kRenderBlockSize), 0.0f);
    std::vector<float> scratchR(static_cast<size_t>(kRenderBlockSize), 0.0f);

    int rendered = 0;
    bool noteReleased = false;
    while (rendered < totalFrames) {
        const int remaining = totalFrames - rendered;
        const int block = std::min(kRenderBlockSize, remaining);

        // Trigger noteOff exactly once at the hold boundary so the amp env
        // enters release for the remaining tail.
        if (!noteReleased && rendered + block >= holdFrames) {
            vm.noteOff(std::clamp(cfg.midi_note, 0, 127));
            noteReleased = true;
        }

        std::fill(scratchL.begin(), scratchL.begin() + block, 0.0f);
        std::fill(scratchR.begin(), scratchR.begin() + block, 0.0f);
        vm.renderBlock(scratchL.data(), scratchR.data(), block);

        for (int i = 0; i < block; ++i) {
            const float l = scratchL[static_cast<size_t>(i)];
            const float r = scratchR[static_cast<size_t>(i)];
            const size_t outIdx = static_cast<size_t>(rendered + i) * static_cast<size_t>(channels);
            if (channels == 2) {
                buffer[outIdx] = l;
                buffer[outIdx + 1] = r;
            } else {
                buffer[outIdx] = 0.5f * (l + r);
            }
        }

        rendered += block;
    }

    return buffer;
}

bool renderPatchToWav(const PatchStruct& patch, const juce::File& path, const BounceConfig& cfg) {
    const auto interleaved = renderPatchToBuffer(patch, cfg);
    const int channels = std::clamp(cfg.channels, 1, 2);
    const int sampleRate = std::max(8000, cfg.sample_rate_hz);
    const int bitDepth = (cfg.bit_depth == 16 || cfg.bit_depth == 24 || cfg.bit_depth == 32) ? cfg.bit_depth : 24;
    if (interleaved.empty())
        return false;
    const int numFrames = static_cast<int>(interleaved.size() / static_cast<size_t>(channels));

    // Pre-ensure parent dir + delete existing target so the WAV writer doesn't
    // append to stale bytes.
    auto parent = path.getParentDirectory();
    if (!parent.isDirectory())
        parent.createDirectory();
    if (path.existsAsFile())
        path.deleteFile();

    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::FileOutputStream>{path.createOutputStream().release()};
    if (!stream)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer{
        wav.createWriterFor(stream.get(),
                            static_cast<double>(sampleRate),
                            static_cast<unsigned int>(channels),
                            bitDepth,
                            {},
                            0)};
    if (!writer) {
        return false;
    }
    // createWriterFor takes ownership of the stream on success.
    stream.release();

    // De-interleave into per-channel float* the writer can consume.
    std::vector<std::vector<float>> deinterleaved(static_cast<size_t>(channels),
                                                  std::vector<float>(static_cast<size_t>(numFrames), 0.0f));
    for (int f = 0; f < numFrames; ++f) {
        for (int c = 0; c < channels; ++c) {
            deinterleaved[static_cast<size_t>(c)][static_cast<size_t>(f)] =
                interleaved[static_cast<size_t>(f) * static_cast<size_t>(channels) + static_cast<size_t>(c)];
        }
    }
    std::vector<const float*> channelPtrs(static_cast<size_t>(channels), nullptr);
    for (int c = 0; c < channels; ++c)
        channelPtrs[static_cast<size_t>(c)] = deinterleaved[static_cast<size_t>(c)].data();

    const bool ok = writer->writeFromFloatArrays(channelPtrs.data(), channels, numFrames);
    // Destructor finalises the WAV header.
    writer.reset();
    return ok;
}

} // namespace agentic_synth::engine
