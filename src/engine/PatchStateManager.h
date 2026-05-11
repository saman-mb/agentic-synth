#pragma once

#include "engine/PatchStruct.h"

#include <juce_core/juce_core.h>

namespace agentic_synth::engine {

// DAW project state save/restore using JUCE ValueTree XML serialization.
// Captures full synth state: patch parameters, mod matrix, UI state.

class PatchStateManager {
public:
    PatchStateManager();

    // Save current state as XML string
    juce::String saveToXml(const PatchStruct& patch);

    // Restore state from XML string
    PatchStruct loadFromXml(const juce::String& xml);

    // Save to file
    bool saveToFile(const juce::File& file, const PatchStruct& patch);

    // Load from file
    PatchStruct loadFromFile(const juce::File& file);

    // Convert to/from JUCE ValueTree
    juce::ValueTree toValueTree(const PatchStruct& patch);
    PatchStruct fromValueTree(const juce::ValueTree& tree);

    // UI state persistence (optional)
    struct UIState {
        float knobPositions[8]{};
        int activeTab = 0;
        float zoomLevel = 1.0f;
    };
    void saveUIState(const UIState& state);
    UIState loadUIState();

private:
    static constexpr const char* kTreeType = "AgenticSynthState";
    static constexpr int kCurrentVersion = 1;
};

} // namespace agentic_synth::engine
