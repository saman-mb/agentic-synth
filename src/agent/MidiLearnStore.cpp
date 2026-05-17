#include "agent/MidiLearnStore.h"

#include <algorithm>

namespace agentic_synth::agent {

namespace {

juce::File defaultMidiMapFile() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AgenticSynth");
    if (!dir.isDirectory())
        dir.createDirectory();
    return dir.getChildFile("midi_map.json");
}

} // namespace

MidiLearnStore& MidiLearnStore::instance() {
    static MidiLearnStore singleton{defaultMidiMapFile()};
    return singleton;
}

MidiLearnStore MidiLearnStore::withFileForTesting(juce::File file) {
    return MidiLearnStore{std::move(file)};
}

MidiLearnStore::MidiLearnStore(juce::File path) : path_(std::move(path)) {
    std::lock_guard<std::mutex> lock(mu_);
    loadLocked();
}

void MidiLearnStore::loadLocked() {
    mappings_.clear();
    if (!path_.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse(path_);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto mapsVar = obj->getProperty("mappings");
    auto* arr = mapsVar.getArray();
    if (arr == nullptr)
        return;

    mappings_.reserve(static_cast<size_t>(arr->size()));
    for (const auto& entry : *arr) {
        auto* eobj = entry.getDynamicObject();
        if (eobj == nullptr)
            continue;
        MidiMapping m;
        m.knob_id = eobj->getProperty("knob_id").toString().toStdString();
        m.cc = static_cast<int>(eobj->getProperty("cc"));
        m.channel = static_cast<int>(eobj->getProperty("channel"));
        if (!m.knob_id.empty() && m.cc >= 0 && m.cc < 128)
            mappings_.push_back(std::move(m));
    }
}

void MidiLearnStore::persistLocked() {
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(mappings_.size()));
    for (const auto& m : mappings_) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("knob_id", juce::String(m.knob_id));
        obj->setProperty("cc", m.cc);
        obj->setProperty("channel", m.channel);
        arr.add(juce::var{obj});
    }
    auto* root = new juce::DynamicObject{};
    root->setProperty("mappings", juce::var{arr});
    const auto text = juce::JSON::toString(juce::var{root});

    auto parent = path_.getParentDirectory();
    if (!parent.isDirectory())
        parent.createDirectory();
    path_.replaceWithText(text);
}

void MidiLearnStore::enterLearnMode(const std::string& knob_id) noexcept {
    if (knob_id.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        learningKnob_ = knob_id;
    }
    learning_.store(true, std::memory_order_release);
}

void MidiLearnStore::cancelLearnMode() noexcept {
    learning_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mu_);
    learningKnob_.clear();
}

std::string MidiLearnStore::learningKnobId() const {
    std::lock_guard<std::mutex> lock(mu_);
    return learningKnob_;
}

std::optional<std::string> MidiLearnStore::captureIfLearning(int cc, int channel) {
    if (!learning_.load(std::memory_order_acquire))
        return std::nullopt;
    if (cc < 0 || cc > 127)
        return std::nullopt;

    // Filter out bank-select / control changes that any DAW emits constantly
    // (CC 0, 32, 120-127). The user wants a tactile knob/fader, not a hidden
    // session message.
    if (cc == 0 || cc == 32 || cc >= 120)
        return std::nullopt;

    std::string captured;
    {
        std::lock_guard<std::mutex> lock(mu_);
        captured = learningKnob_;
        if (captured.empty())
            return std::nullopt;

        // Remove any existing mapping for this knob OR this CC — both
        // directions are 1:1 so the user can re-learn / re-assign cleanly.
        mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
                                       [&](const MidiMapping& m) {
                                           return m.knob_id == captured ||
                                                  (m.cc == cc && m.channel == channel);
                                       }),
                        mappings_.end());

        MidiMapping m;
        m.knob_id = captured;
        m.cc = cc;
        m.channel = channel;
        mappings_.push_back(std::move(m));
        persistLocked();

        learningKnob_.clear();
    }
    learning_.store(false, std::memory_order_release);
    return captured;
}

std::optional<std::string> MidiLearnStore::findKnobFor(int cc, int channel) const {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& m : mappings_) {
        if (m.cc == cc && (m.channel == channel || m.channel < 0))
            return m.knob_id;
    }
    return std::nullopt;
}

void MidiLearnStore::clearMapping(const std::string& knob_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto before = mappings_.size();
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
                                   [&](const MidiMapping& m) { return m.knob_id == knob_id; }),
                    mappings_.end());
    if (mappings_.size() != before)
        persistLocked();
}

std::vector<MidiMapping> MidiLearnStore::all() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mappings_;
}

} // namespace agentic_synth::agent
