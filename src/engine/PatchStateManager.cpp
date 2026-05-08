#include "PatchStateManager.h"

namespace agentic_synth::engine {

PatchStateManager::PatchStateManager() = default;

juce::String PatchStateManager::saveToXml(const PatchStruct& patch) {
    auto tree = toValueTree(patch);
    auto xml = tree.createXml();
    return xml->toString();
}

PatchStruct PatchStateManager::loadFromXml(const juce::String& xml) {
    auto xml = juce::XmlDocument::parse(xml);
    if (xml == nullptr)
        return PatchStruct{};

    auto tree = juce::ValueTree::fromXml(*xml);
    return fromValueTree(tree);
}

bool PatchStateManager::saveToFile(const juce::File& file, const PatchStruct& patch) {
    auto xml = toValueTree(patch).createXml();
    return xml->writeTo(file, {});
}

PatchStruct PatchStateManager::loadFromFile(const juce::File& file) {
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return PatchStruct{};

    auto tree = juce::ValueTree::fromXml(*xml);
    return fromValueTree(tree);
}

juce::ValueTree PatchStateManager::toValueTree(const PatchStruct& patch) {
    juce::ValueTree tree(kTreeType);
    tree.setProperty("version", kCurrentVersion, nullptr);

    // Store each field as a named property
    tree.setProperty("filterCutoffHz", patch.filterCutoffHz, nullptr);
    tree.setProperty("filterResonance", patch.filterResonance, nullptr);
    tree.setProperty("ampAttackMs", patch.ampAttackMs, nullptr);
    tree.setProperty("ampDecayMs", patch.ampDecayMs, nullptr);
    tree.setProperty("ampSustainLevel", patch.ampSustainLevel, nullptr);
    tree.setProperty("ampReleaseMs", patch.ampReleaseMs, nullptr);
    tree.setProperty("lfoRateHz", patch.lfoRateHz, nullptr);
    tree.setProperty("lfoDepth", patch.lfoDepth, nullptr);
    tree.setProperty("lfoShape", static_cast<int>(patch.lfoShape), nullptr);

    // Oscillator mix as array
    juce::Array<juce::var> oscMix;
    for (int i = 0; i < 5; ++i)
        oscMix.add(patch.oscillatorMix[i]);
    tree.setProperty("oscillatorMix", oscMix, nullptr);

    return tree;
}

PatchStruct PatchStateManager::fromValueTree(const juce::ValueTree& tree) {
    if (!tree.hasType(kTreeType))
        return PatchStruct{};

    PatchStruct patch{};
    patch.filterCutoffHz = tree.getProperty("filterCutoffHz", 500.0f);
    patch.filterResonance = tree.getProperty("filterResonance", 0.0f);
    patch.ampAttackMs = tree.getProperty("ampAttackMs", 10.0f);
    patch.ampDecayMs = tree.getProperty("ampDecayMs", 100.0f);
    patch.ampSustainLevel = tree.getProperty("ampSustainLevel", 1.0f);
    patch.ampReleaseMs = tree.getProperty("ampReleaseMs", 500.0f);
    patch.lfoRateHz = tree.getProperty("lfoRateHz", 5.0f);
    patch.lfoDepth = tree.getProperty("lfoDepth", 0.0f);
    patch.lfoShape = static_cast<decltype(patch.lfoShape)>(static_cast<int>(tree.getProperty("lfoShape", 0)));

    auto oscMix = tree.getProperty("oscillatorMix", juce::Array<juce::var>());
    if (auto* arr = oscMix.getArray()) {
        for (int i = 0; i < std::min(arr->size(), 5); ++i) {
            patch.oscillatorMix[i] = static_cast<float>((*arr)[i]);
        }
    }

    return patch;
}

void PatchStateManager::saveUIState(const UIState& state) {
    (void)state;
    // TODO: persist UI state to file
}

PatchStateManager::UIState PatchStateManager::loadUIState() { return UIState{}; }

} // namespace agentic_synth::engine
