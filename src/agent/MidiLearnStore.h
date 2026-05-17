#pragma once

// Phase G / #262 — MIDI learn. User right-clicks a knob, picks "Learn MIDI",
// wiggles a physical CC; the next incoming CC is captured and persisted as a
// `{ knob_id, cc_number, channel }` mapping. Subsequent CC events drive that
// knob automatically.
//
// Persistence shape mirrors PresetStore — JSON file at
// <appData>/AgenticSynth/midi_map.json, schema:
//   {
//     "mappings": [
//       { "knob_id": "filter.cutoff", "cc": 74, "channel": 0 }, ...
//     ]
//   }
//
// Learn mode is a tiny atomic flag — the engine asks "is anything learning?"
// per incoming CC, and if so, captures the first non-bank-select CC and
// returns the captured knob id so the agent layer can notify the UI.

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace agentic_synth::agent {

struct MidiMapping {
    std::string knob_id;
    int cc{-1};
    int channel{0}; // 0 = MIDI ch 1 (omni-style). Stored 0..15.
};

class MidiLearnStore {
public:
    // Process-wide singleton — every WebUiComponent instance and the
    // engine::MidiHandler observer must agree on the same map.
    static MidiLearnStore& instance();

    // Test hook: build a store rooted at an arbitrary file. Useful for unit
    // tests that need temporary state.
    static MidiLearnStore withFileForTesting(juce::File file);

    // Enter learn mode for `knob_id`. The next captureIfLearning() call will
    // claim that knob. Calling enterLearnMode again with a different id
    // simply repoints the in-flight capture — last writer wins (the UI
    // typically only allows one "Learn" at a time anyway).
    void enterLearnMode(const std::string& knob_id) noexcept;

    // Cancel learn mode without capturing. UI calls this when the user
    // closes the context menu or switches knobs.
    void cancelLearnMode() noexcept;

    [[nodiscard]] bool isLearning() const noexcept { return learning_.load(std::memory_order_acquire); }
    [[nodiscard]] std::string learningKnobId() const;

    // Engine MIDI thread entry point. Called for every incoming CC change.
    // Returns the captured knob id when learn mode was active and this CC
    // was the first one observed; nullopt otherwise. The captured mapping
    // is persisted immediately.
    [[nodiscard]] std::optional<std::string> captureIfLearning(int cc, int channel);

    // Lookup: does this CC have a knob mapped? Returns nullopt when not
    // mapped. Channel match: stored channel must equal the CC's channel,
    // OR stored channel == -1 (omni — not exposed in the MVP but reserved).
    [[nodiscard]] std::optional<std::string> findKnobFor(int cc, int channel) const;

    // Drop a mapping by knob id. No-op when absent.
    void clearMapping(const std::string& knob_id);

    // Snapshot of every mapping, copied for return so callers don't hold
    // the store's mutex while iterating.
    [[nodiscard]] std::vector<MidiMapping> all() const;

private:
    explicit MidiLearnStore(juce::File path);

    void persistLocked();
    void loadLocked();

    mutable std::mutex mu_;
    std::vector<MidiMapping> mappings_;
    juce::File path_;

    // Learn-mode flags. learningKnob_ guarded by mu_ on writes; readers
    // copy the string under the lock. learning_ is an atomic so the audio
    // / MIDI thread can short-circuit captureIfLearning() without taking
    // the mutex on every CC.
    std::atomic<bool> learning_{false};
    std::string learningKnob_;
};

} // namespace agentic_synth::agent
