// Phase D / #260 — PresetStore unit coverage.
//
// Validates save/all/getByName/deleteByName plus duplicate-overwrite and
// thread-safety under concurrent save() calls. Tests are run against a
// temp-file PresetStore (withFileForTesting) so the real on-disk presets
// store under userApplicationDataDirectory is never touched.

#include "agent/PresetStore.h"
#include "engine/PatchStruct.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using agentic_synth::agent::PresetStore;
using agentic_synth::agent::StoredPreset;
using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;

namespace {

juce::File tempPresetsFile(const char* tag) {
    static std::atomic<int> counter{0};
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("AgenticSynthPresetStoreTest")
                   .getChildFile(juce::String(tag) + "-" + juce::String(stamp) + "-" + juce::String(id));
    dir.createDirectory();
    return dir.getChildFile("presets.json");
}

PatchStruct distinctPatch(float cutoff, float reverbMix) {
    auto p = make_default_patch();
    p.filter.cutoff_hz = cutoff;
    p.reverb.mix = reverbMix;
    return p;
}

} // namespace

TEST_CASE("PresetStore: empty on first instantiation against a fresh file", "[PresetStore]") {
    auto file = tempPresetsFile("empty");
    auto store = PresetStore::withFileForTesting(file);
    CHECK(store.all().empty());
    CHECK_FALSE(store.getByName("anything").has_value());
}

TEST_CASE("PresetStore: save then all() returns the saved entry", "[PresetStore]") {
    auto file = tempPresetsFile("save");
    auto store = PresetStore::withFileForTesting(file);

    store.save("warm pad #1", "warm cinematic pad", distinctPatch(1200.0f, 0.4f));
    const auto all = store.all();
    REQUIRE(all.size() == 1);
    CHECK(all[0].name == "warm pad #1");
    CHECK(all[0].prompt == "warm cinematic pad");
    CHECK(all[0].patch.filter.cutoff_hz == 1200.0f);
    CHECK(all[0].patch.reverb.mix == 0.4f);
    CHECK(all[0].created_ms > 0);
}

TEST_CASE("PresetStore: getByName finds the right preset", "[PresetStore]") {
    auto file = tempPresetsFile("get");
    auto store = PresetStore::withFileForTesting(file);

    store.save("alpha", "alpha prompt", distinctPatch(800.0f, 0.1f));
    store.save("beta", "beta prompt", distinctPatch(8000.0f, 0.7f));

    auto a = store.getByName("alpha");
    REQUIRE(a.has_value());
    CHECK(a->prompt == "alpha prompt");
    CHECK(a->patch.filter.cutoff_hz == 800.0f);

    auto b = store.getByName("beta");
    REQUIRE(b.has_value());
    CHECK(b->prompt == "beta prompt");
    CHECK(b->patch.filter.cutoff_hz == 8000.0f);

    CHECK_FALSE(store.getByName("missing").has_value());
}

TEST_CASE("PresetStore: deleteByName removes the entry, no-op when absent", "[PresetStore]") {
    auto file = tempPresetsFile("delete");
    auto store = PresetStore::withFileForTesting(file);

    store.save("doomed", "p1", distinctPatch(500.0f, 0.0f));
    store.save("survivor", "p2", distinctPatch(2000.0f, 0.2f));

    store.deleteByName("doomed");
    CHECK_FALSE(store.getByName("doomed").has_value());
    REQUIRE(store.all().size() == 1);
    CHECK(store.all()[0].name == "survivor");

    // No-op delete must not throw or trim.
    store.deleteByName("nonexistent");
    CHECK(store.all().size() == 1);
}

TEST_CASE("PresetStore: duplicate name overwrites (last write wins)", "[PresetStore]") {
    auto file = tempPresetsFile("dup");
    auto store = PresetStore::withFileForTesting(file);

    store.save("same", "first prompt", distinctPatch(500.0f, 0.1f));
    store.save("same", "second prompt", distinctPatch(9000.0f, 0.9f));

    const auto all = store.all();
    REQUIRE(all.size() == 1);
    CHECK(all[0].prompt == "second prompt");
    CHECK(all[0].patch.filter.cutoff_hz == 9000.0f);
    CHECK(all[0].patch.reverb.mix == 0.9f);
}

TEST_CASE("PresetStore: 10 concurrent save() calls all complete without corruption",
          "[PresetStore][Threading]") {
    auto file = tempPresetsFile("threads");
    auto store = PresetStore::withFileForTesting(file);

    constexpr int kThreads = 10;
    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&store, i]() {
            store.save("preset-" + std::to_string(i), "prompt " + std::to_string(i),
                       distinctPatch(static_cast<float>(500 + i * 100),
                                     static_cast<float>(i) / static_cast<float>(kThreads)));
        });
    }
    for (auto& t : ts)
        t.join();

    const auto all = store.all();
    CHECK(all.size() == static_cast<size_t>(kThreads));
    for (int i = 0; i < kThreads; ++i) {
        const auto name = "preset-" + std::to_string(i);
        auto sp = store.getByName(name);
        REQUIRE(sp.has_value());
        CHECK(sp->prompt == "prompt " + std::to_string(i));
    }
}

TEST_CASE("PresetStore: state survives reload from disk", "[PresetStore][Persistence]") {
    auto file = tempPresetsFile("persist");
    {
        auto store = PresetStore::withFileForTesting(file);
        store.save("persistent", "from disk", distinctPatch(4321.0f, 0.55f));
    }
    // Fresh instance reads the same file.
    auto store2 = PresetStore::withFileForTesting(file);
    auto sp = store2.getByName("persistent");
    REQUIRE(sp.has_value());
    CHECK(sp->prompt == "from disk");
    CHECK(sp->patch.filter.cutoff_hz == 4321.0f);
    CHECK(sp->patch.reverb.mix == 0.55f);
}
