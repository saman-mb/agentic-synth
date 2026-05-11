#pragma once

#include <string>

#include "mapper/SemanticMapper.h"

namespace agentic_synth::agent {

// Phase 10C: extracted from AgentBridge to address god-class concerns.
//
// Single responsibility: serve and persist the semantic-mapper custom
// dictionary. Wraps the SemanticMapper for the three operations the UI
// exposes — getDictionaryJson(), saveDictionary(json), loadDictionary(path).
//
// Composition (not inheritance): AgentBridge owns a DictionaryService that
// holds a non-owning reference to the AgentBridge-owned SemanticMapper.
// Lifetime is bound to AgentBridge — DictionaryService must not outlive its
// mapper. No audio-thread state lives here; all calls run on the UI / control
// thread (WebUiComponent native functions, tests).
class DictionaryService {
public:
    explicit DictionaryService(mapper::SemanticMapper& mapper);

    // Static + custom entries serialised as a dictionary_data JSON frame.
    [[nodiscard]] std::string getDictionaryJson() const;

    // Parse custom entries from a save_dictionary JSON frame and persist to
    // the default on-disk path (descriptor_dataset_custom.json).
    void saveDictionary(const std::string& json);

    // Load custom entries from disk. Defaults to the same path saveDictionary
    // writes to so a startup-load + tweak + save round-trip is idempotent.
    void loadDictionary(const std::string& path = "descriptor_dataset_custom.json");

private:
    mapper::SemanticMapper& mapper_;
};

} // namespace agentic_synth::agent
