#pragma once

#include "engine/PatchStruct.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace agentic_synth::agent {

// Phase D / #260 — preset commit + persistence.
//
// StoredPreset is the on-disk shape: a name (display label), the most recent
// user prompt that produced the patch, the full PatchStruct (POD copy), and
// a creation timestamp in unix epoch milliseconds. Tags/parent-genome from
// the original spec are deferred — MVP is name + prompt + patch + timestamp.
struct StoredPreset {
    std::string name;
    std::string prompt;
    PatchStruct patch{};
    std::int64_t created_ms{0};
};

// Thread-safe JSON-backed preset store. Singleton because every WebUiComponent
// instance (multi-DAW-track case) sees the same on-disk file; we serialise
// writes through one process-wide mutex to avoid corrupting the JSON.
//
// Storage: <userApplicationDataDirectory>/AgenticSynth/presets.json
// Schema:  { "presets": [ { "name", "prompt", "patch", "created_ms" }, ... ] }
//
// Save semantics: duplicate name overwrites the existing entry (last write
// wins) — the UI is expected to suggest a unique name on commit, so this
// path is rare but well-defined.
class PresetStore {
public:
    // Process-wide singleton. The file is loaded lazily on first call.
    static PresetStore& instance();

    // Test hook: build a store rooted at an arbitrary file. The singleton
    // remains untouched. Useful for unit tests that need temporary state.
    static PresetStore withFileForTesting(juce::File file);

    // Append or overwrite the preset with this name. Persists immediately
    // (full file rewrite). Thread-safe.
    void save(const std::string& name, const std::string& prompt, const PatchStruct& patch);

    // Full snapshot of every preset in insertion order. Returns by value
    // so callers don't need to hold the store's mutex.
    [[nodiscard]] std::vector<StoredPreset> all() const;

    // Lookup by name; nullopt when not present.
    [[nodiscard]] std::optional<StoredPreset> getByName(const std::string& name) const;

    // Remove a preset by name. No-op when absent. Persists immediately.
    void deleteByName(const std::string& name);

private:
    explicit PresetStore(juce::File path);

    void persistLocked();
    void loadLocked();

    mutable std::mutex mu_;
    std::vector<StoredPreset> presets_;
    juce::File path_;
};

} // namespace agentic_synth::agent
