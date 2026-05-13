#pragma once

#include "agent/AgentBridge.h"
#include "engine/ADSREnvelope.h"
#include "engine/MidiHandler.h"
#include "engine/PatchStruct.h"
#include "engine/SPSCQueue.h"
#include "engine/VoiceManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AgenticSynthPlugin final : public juce::AudioProcessor, private juce::Timer {
public:
    AgenticSynthPlugin();
    ~AgenticSynthPlugin() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }

    // Phase 4: editor needs access to the AgentBridge so the WebView native
    // bridge can submit prompts, record feedback, push knob tweaks, etc.
    agentic_synth::agent::AgentBridge& agentBridge() noexcept { return agentBridge_; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Audition queue sizing (public so test accessors can name the type) ──
    static constexpr std::size_t kAuditionQueueCapacity = 256;
    static constexpr int kAuditionDrainBudget = 64;

    // Phase 12: Visualizer audio tap.
    //
    // Audio thread pushes mono-summed post-amp samples into scopeQueue_ at
    // audio rate (worst case ~96 kHz × 1 push/sample). The message thread
    // drains the queue via pullScopeSamples() in response to a JS bridge
    // call at ~60 Hz. Capacity must be large enough that at typical sample
    // rates, one block (max ~1024) plus a 16ms-worth-of-samples margin fits
    // without dropping. 4096 = ~85 ms of headroom at 48 kHz, comfortable
    // versus 60 fps polling.
    static constexpr std::size_t kScopeQueueCapacity = 4096;
    using ScopeQueueT = agentic_synth::SPSCQueue<float, kScopeQueueCapacity>;

    // Message thread: drain up to `max` samples into `out`. Wait-free SPSC
    // pop, no allocation, no lock. Returns the number of samples popped.
    // `out` is cleared first via setSize. Safe to call concurrently with
    // the audio thread (single producer / single consumer contract).
    int pullScopeSamples(float* dest, int max) noexcept;

    // Phase 2 (Item #4): message-thread poll cadence for AgentBridge → APVTS.
    // 20 ms is well below the perceptual threshold for a "delayed" AI patch
    // and matches GUI refresh budget. AgentBridge::pollPatch is a wait-free
    // SPSC pop so we are not paying for blocking syscalls here.
    static constexpr int kAgentBridgePollIntervalMs = 20;

    // ── Test-only hooks ──────────────────────────────────────────────────────
    using AuditionQueueT = agentic_synth::SPSCQueue<agentic_synth::engine::RawMidiMsg, kAuditionQueueCapacity>;
    [[nodiscard]] AuditionQueueT& auditionQueueForTest() noexcept { return auditionQueue_; }
    [[nodiscard]] static constexpr int auditionDrainBudgetForTest() noexcept { return kAuditionDrainBudget; }
    [[nodiscard]] agentic_synth::engine::VoiceManager& voiceManagerForTest() noexcept { return voiceManager_; }
    [[nodiscard]] agentic_synth::engine::MidiHandler& midiHandlerForTest() noexcept { return midiHandler_; }

    // Phase 2: helpers exposed for tests so the APVTS-roundtrip assertions
    // don't have to duplicate the param-name table.
    [[nodiscard]] agentic_synth::PatchStruct buildPatchFromApvtsForTest() const noexcept {
        return buildPatchFromApvts();
    }
    void writePatchToApvtsForTest(const agentic_synth::PatchStruct& p) { writePatchToApvts(p); }
    // Drain one cycle of the AgentBridge → APVTS pump synchronously. Tests
    // can call this in place of waiting for the juce::Timer to fire.
    void pumpAgentBridgeForTest() { pumpAgentBridgePatchQueue(); }

private:
    // Phase 2: APVTS is now the SOLE writer of engine parameter state.
    //   - applyParameters() reads every APVTS param into a PatchStruct and
    //     pushes it to VoiceManager::applyPatch in one go.
    //   - writePatchToApvts() takes a PatchStruct (e.g. from the AI pipeline)
    //     and pushes each field via setValueNotifyingHost so the host sees
    //     the AI change as an automation event AND so the audio thread reads
    //     it back through the normal applyParameters() path next block.
    void applyParameters() noexcept;
    [[nodiscard]] agentic_synth::PatchStruct buildPatchFromApvts() const noexcept;
    void writePatchToApvts(const agentic_synth::PatchStruct& patch);

    // juce::Timer hook: drains AgentBridge::pollPatch on the message thread
    // and routes the patch through writePatchToApvts(). Replaces the
    // audio-thread pollPatch + direct-engine-write that existed in Phase 1.
    void timerCallback() override;
    void pumpAgentBridgePatchQueue();

    agentic_synth::engine::VoiceManager voiceManager_;
    agentic_synth::agent::AgentBridge agentBridge_;
    agentic_synth::engine::MidiHandler midiHandler_;
    juce::AudioProcessorValueTreeState apvts_;

    // Lock-free FIFO for non-audio-thread MIDI producers (audition keyboard,
    // future remote control). Producer = JUCE message / Timer thread; consumer
    // = processBlock on the audio thread. SPSCQueue is wait-free + alloc-free
    // — satisfies the RT rule against locks and allocs.
    agentic_synth::SPSCQueue<agentic_synth::engine::RawMidiMsg, kAuditionQueueCapacity> auditionQueue_;

    // Phase 12: post-amp visualizer tap. Producer = audio thread (push after
    // master gain in processBlock). Consumer = message thread (drained from
    // the WebView native `getScopeSamples` handler via pullScopeSamples).
    // Drops silently when full — losing visualizer frames is preferable to
    // blocking the audio thread.
    ScopeQueueT scopeQueue_;

    // ── APVTS typed-pointer cache ───────────────────────────────────────────
    // Phase 2 follow-up (Code Fix 1): cache concrete parameter types instead
    // of RangedAudioParameter* + audio-thread static_cast. cacheParameterHandles
    // dynamic_casts each handle once, jasserts non-null, and stores the typed
    // pointer — the audio thread then dereferences without any cast. If a
    // param ID is ever wired to the wrong concrete type the assert fires at
    // construction, not as UB on the RT path.
    //
    // Floats remain std::atomic<float>* (raw getRawParameterValue handle,
    // RT-safe) — that path was already correct.

    // Global
    std::atomic<float>* masterGainParam_ = nullptr;
    std::atomic<float>* portamentoParam_ = nullptr;
    juce::AudioParameterInt* voiceCountParam_ = nullptr;

    // Filter
    juce::AudioParameterChoice* filterTypeParam_ = nullptr;
    std::atomic<float>* filterCutoffParam_ = nullptr;
    std::atomic<float>* filterResParam_ = nullptr;
    std::atomic<float>* filterEnvModParam_ = nullptr;
    std::atomic<float>* filterKeyTrackParam_ = nullptr;
    std::atomic<float>* filterDriveParam_ = nullptr;

    // Amp envelope
    std::atomic<float>* ampAttackParam_ = nullptr;
    std::atomic<float>* ampDecayParam_ = nullptr;
    std::atomic<float>* ampSustainParam_ = nullptr;
    std::atomic<float>* ampReleaseParam_ = nullptr;

    // Filter envelope
    std::atomic<float>* filterEnvAttackParam_ = nullptr;
    std::atomic<float>* filterEnvDecayParam_ = nullptr;
    std::atomic<float>* filterEnvSustainParam_ = nullptr;
    std::atomic<float>* filterEnvReleaseParam_ = nullptr;

    // Per-oscillator caches (3 oscillators × 10 fields = 30 params).
    struct OscParamCache {
        juce::AudioParameterChoice* type = nullptr;
        std::atomic<float>* detune_cents = nullptr;
        std::atomic<float>* semis = nullptr;
        std::atomic<float>* wavetable_pos = nullptr;
        std::atomic<float>* fm_ratio = nullptr;
        std::atomic<float>* fm_depth = nullptr;
        std::atomic<float>* pulse_width = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* volume = nullptr;
        juce::AudioParameterBool* enabled = nullptr;
    };
    std::array<OscParamCache, agentic_synth::kMaxOscillators> oscParams_{};

    // Per-LFO caches (2 LFOs × 6 fields = 12 params).
    struct LfoParamCache {
        juce::AudioParameterChoice* waveform = nullptr;
        juce::AudioParameterChoice* target = nullptr;
        std::atomic<float>* rate_hz = nullptr;
        std::atomic<float>* depth = nullptr;
        std::atomic<float>* phase = nullptr;
        juce::AudioParameterBool* bpm_sync = nullptr;
    };
    std::array<LfoParamCache, agentic_synth::kMaxLfos> lfoParams_{};

    // Reverb
    std::atomic<float>* reverbSizeParam_ = nullptr;
    std::atomic<float>* reverbDampingParam_ = nullptr;
    std::atomic<float>* reverbWidthParam_ = nullptr;
    std::atomic<float>* reverbMixParam_ = nullptr;

    // Delay
    std::atomic<float>* delayTimeParam_ = nullptr;
    std::atomic<float>* delayFeedbackParam_ = nullptr;
    std::atomic<float>* delayMixParam_ = nullptr;
    std::atomic<float>* delayStereoParam_ = nullptr;
    juce::AudioParameterBool* delayBpmSyncParam_ = nullptr;

    void cacheParameterHandles() noexcept;

    // Phase 2 follow-up (Code Fix 6): writePatchToApvts (message thread)
    // sets this flag after pushing a new AI patch. The audio thread sees it
    // in applyParameters() at the top of processBlock, calls primeSmoothers()
    // ON the audio thread (the only writer of smoother state) and clears the
    // flag. This avoids the race that would exist if the message thread
    // called primeSmoothers directly while processBlock was running.
    std::atomic<bool> pendingPrime_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgenticSynthPlugin)
};
