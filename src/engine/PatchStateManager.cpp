#include "PatchStateManager.h"

namespace agentic_synth::engine {

PatchStateManager::PatchStateManager() = default;

juce::String PatchStateManager::saveToXml(const PatchStruct& patch) {
    auto tree = toValueTree(patch);
    auto xml = tree.createXml();
    return xml->toString();
}

PatchStruct PatchStateManager::loadFromXml(const juce::String& xmlStr) {
    auto xmlDoc = juce::XmlDocument::parse(xmlStr);
    if (xmlDoc == nullptr)
        return PatchStruct{};

    auto tree = juce::ValueTree::fromXml(*xmlDoc);
    return fromValueTree(tree);
}

bool PatchStateManager::saveToFile(const juce::File& file, const PatchStruct& patch) {
    auto xml = toValueTree(patch).createXml();
    return xml->writeTo(file, {});
}

PatchStruct PatchStateManager::loadFromFile(const juce::File& file) {
    auto xmlDoc = juce::XmlDocument::parse(file);
    if (xmlDoc == nullptr)
        return PatchStruct{};

    auto tree = juce::ValueTree::fromXml(*xmlDoc);
    return fromValueTree(tree);
}

juce::ValueTree PatchStateManager::toValueTree(const PatchStruct& patch) {
    juce::ValueTree tree(kTreeType);
    tree.setProperty("version", kCurrentVersion, nullptr);

    tree.setProperty("filter_cutoff_hz", patch.filter.cutoff_hz, nullptr);
    tree.setProperty("filter_resonance", patch.filter.resonance, nullptr);
    tree.setProperty("amp_attack_s", patch.amp_env.attack_s, nullptr);
    tree.setProperty("amp_decay_s", patch.amp_env.decay_s, nullptr);
    tree.setProperty("amp_sustain", patch.amp_env.sustain, nullptr);
    tree.setProperty("amp_release_s", patch.amp_env.release_s, nullptr);
    tree.setProperty("lfo0_rate_hz", patch.lfo[0].rate_hz, nullptr);
    tree.setProperty("lfo0_depth", patch.lfo[0].depth, nullptr);
    tree.setProperty("lfo0_waveform", static_cast<int>(patch.lfo[0].waveform), nullptr);

    juce::Array<juce::var> oscVolumes;
    for (int i = 0; i < kMaxOscillators; ++i)
        oscVolumes.add(patch.osc[i].volume);
    tree.setProperty("osc_volumes", oscVolumes, nullptr);

    return tree;
}

PatchStruct PatchStateManager::fromValueTree(const juce::ValueTree& tree) {
    if (!tree.hasType(kTreeType))
        return PatchStruct{};

    PatchStruct patch{};
    patch.filter.cutoff_hz  = tree.getProperty("filter_cutoff_hz", 500.0f);
    patch.filter.resonance  = tree.getProperty("filter_resonance", 0.0f);
    patch.amp_env.attack_s  = tree.getProperty("amp_attack_s", 0.005f);
    patch.amp_env.decay_s   = tree.getProperty("amp_decay_s", 0.1f);
    patch.amp_env.sustain   = tree.getProperty("amp_sustain", 1.0f);
    patch.amp_env.release_s = tree.getProperty("amp_release_s", 0.1f);
    patch.lfo[0].rate_hz    = tree.getProperty("lfo0_rate_hz", 1.0f);
    patch.lfo[0].depth      = tree.getProperty("lfo0_depth", 0.0f);
    patch.lfo[0].waveform   = static_cast<LfoWaveform>(static_cast<int>(tree.getProperty("lfo0_waveform", 0)));

    auto oscVolumes = tree.getProperty("osc_volumes", juce::Array<juce::var>());
    if (auto* arr = oscVolumes.getArray()) {
        for (int i = 0; i < std::min(arr->size(), kMaxOscillators); ++i)
            patch.osc[i].volume = static_cast<float>((*arr)[i]);
    }

    return patch;
}

void PatchStateManager::saveUIState(const UIState& state) {
    (void)state;
    // TODO: persist UI state to file
}

PatchStateManager::UIState PatchStateManager::loadUIState() { return UIState{}; }

} // namespace agentic_synth::engine
