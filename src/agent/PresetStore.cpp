#include "agent/PresetStore.h"

#include "agent/AgentBridge.h"

#include <algorithm>
#include <chrono>

namespace agentic_synth::agent {

namespace {

// Resolve the canonical presets.json path.  Mirrors the
// `agenticSynthAppDataDir` helper inside WebUiComponent.cpp — we duplicate the
// resolution rather than reach across because pulling juce_gui_extra into the
// agent library would bloat headless / engine builds.
juce::File defaultPresetsFile() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AgenticSynth");
    if (!dir.isDirectory())
        dir.createDirectory();
    return dir.getChildFile("presets.json");
}

std::int64_t nowMs() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

PresetStore& PresetStore::instance() {
    static PresetStore singleton{defaultPresetsFile()};
    return singleton;
}

PresetStore PresetStore::withFileForTesting(juce::File file) {
    return PresetStore{std::move(file)};
}

PresetStore::PresetStore(juce::File path) : path_(std::move(path)) {
    std::lock_guard<std::mutex> lock(mu_);
    loadLocked();
}

void PresetStore::loadLocked() {
    presets_.clear();
    if (!path_.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse(path_);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto presetsVar = obj->getProperty("presets");
    auto* arr = presetsVar.getArray();
    if (arr == nullptr)
        return;

    presets_.reserve(static_cast<size_t>(arr->size()));
    for (const auto& entry : *arr) {
        auto* eobj = entry.getDynamicObject();
        if (eobj == nullptr)
            continue;

        StoredPreset sp;
        sp.name = eobj->getProperty("name").toString().toStdString();
        sp.prompt = eobj->getProperty("prompt").toString().toStdString();
        sp.created_ms = static_cast<std::int64_t>(static_cast<double>(eobj->getProperty("created_ms")));

        const auto patchVar = eobj->getProperty("patch");
        sp.patch = AgentBridge::patchFromVar(patchVar);

        if (!sp.name.empty())
            presets_.push_back(std::move(sp));
    }
}

void PresetStore::persistLocked() {
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(presets_.size()));
    for (const auto& sp : presets_) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("name", juce::String(sp.name));
        obj->setProperty("prompt", juce::String(sp.prompt));
        obj->setProperty("created_ms", static_cast<double>(sp.created_ms));
        obj->setProperty("patch", AgentBridge::patchToVar(sp.patch));
        arr.add(juce::var{obj});
    }

    auto* root = new juce::DynamicObject{};
    root->setProperty("presets", juce::var{arr});
    const auto text = juce::JSON::toString(juce::var{root});

    // Ensure parent dir exists (defensive — singleton ctor creates it, but a
    // test fixture may point us elsewhere).
    auto parent = path_.getParentDirectory();
    if (!parent.isDirectory())
        parent.createDirectory();

    path_.replaceWithText(text);
}

void PresetStore::save(const std::string& name, const std::string& prompt, const PatchStruct& patch) {
    if (name.empty())
        return;

    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
                           [&](const StoredPreset& sp) { return sp.name == name; });
    if (it != presets_.end()) {
        // Last-write-wins on duplicate name.
        it->prompt = prompt;
        it->patch = patch;
        it->created_ms = nowMs();
    } else {
        StoredPreset sp;
        sp.name = name;
        sp.prompt = prompt;
        sp.patch = patch;
        sp.created_ms = nowMs();
        presets_.push_back(std::move(sp));
    }
    persistLocked();
}

std::vector<StoredPreset> PresetStore::all() const {
    std::lock_guard<std::mutex> lock(mu_);
    return presets_;
}

std::optional<StoredPreset> PresetStore::getByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
                           [&](const StoredPreset& sp) { return sp.name == name; });
    if (it == presets_.end())
        return std::nullopt;
    return *it;
}

void PresetStore::deleteByName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
                           [&](const StoredPreset& sp) { return sp.name == name; });
    if (it == presets_.end())
        return;
    presets_.erase(it);
    persistLocked();
}

} // namespace agentic_synth::agent
