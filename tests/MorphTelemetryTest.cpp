#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

#include "agent/MorphTelemetry.h"

using namespace agentic_synth::agent;

namespace {

// Per-test isolated path. Each TEST_CASE points the singleton at its own
// tmpfile so they don't interfere when ctest fans out in parallel.
std::string makeTempPath(const std::string& suffix) {
    auto dir = std::filesystem::temp_directory_path() / "agentic_synth_morph_telemetry";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    static std::atomic<uint64_t> ctr{0};
    return (dir / ("t_" + std::to_string(ctr.fetch_add(1)) + "_" + suffix + ".jsonl")).string();
}

// Reset the singleton state for a clean test run: enable + new path.
void resetTelemetry(const std::string& path) {
    MorphTelemetry::instance().setPathForTest(path);
    MorphTelemetry::instance().setEnabled(true);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            out.push_back(line);
    }
    return out;
}

} // namespace

TEST_CASE("MorphTelemetry: instance() returns the same singleton", "[morph_telemetry]") {
    auto& a = MorphTelemetry::instance();
    auto& b = MorphTelemetry::instance();
    REQUIRE(&a == &b);
}

TEST_CASE("MorphTelemetry: first instance() call has empty store on the new test path", "[morph_telemetry]") {
    const auto path = makeTempPath("empty");
    resetTelemetry(path);
    // No log calls yet — file should not exist (or be empty if a prior run
    // touched it; we removed it in resetTelemetry).
    const auto lines = readLines(path);
    REQUIRE(lines.empty());
}

TEST_CASE("MorphTelemetry: recordMorphRequest writes one JSONL line", "[morph_telemetry]") {
    const auto path = makeTempPath("one_line");
    resetTelemetry(path);

    MorphEvent ev;
    ev.kind = MorphEventKind::MorphRequested;
    ev.prompt_hash = MorphTelemetry::hashPrompt("warm analog pad");
    ev.history_size = 3;
    ev.liked_size = 1;
    MorphTelemetry::instance().log(ev);

    const auto lines = readLines(path);
    REQUIRE(lines.size() == 1);

    auto parsed = juce::JSON::parse(juce::String(lines[0]));
    REQUIRE(parsed.isObject());
    REQUIRE(parsed["kind"].toString() == juce::String("morph_requested"));
    REQUIRE(static_cast<int>(parsed["history_size"]) == 3);
    REQUIRE(static_cast<int>(parsed["liked_size"]) == 1);
    REQUIRE(parsed["prompt_hash"].toString().length() == 8);
}

TEST_CASE("MorphTelemetry: multiple events accumulate in append order", "[morph_telemetry]") {
    const auto path = makeTempPath("ordering");
    resetTelemetry(path);

    auto& t = MorphTelemetry::instance();

    MorphEvent a;
    a.kind = MorphEventKind::MorphRequested;
    a.prompt_hash = MorphTelemetry::hashPrompt("first");
    t.log(a);

    MorphEvent b;
    b.kind = MorphEventKind::VariationPicked;
    b.strategy_id = 2;
    b.label = "warmer";
    b.time_since_arrival_ms = 1200;
    t.log(b);

    MorphEvent c;
    c.kind = MorphEventKind::MacroTweaked;
    c.macro_index = 1;
    c.value = 0.42f;
    c.dwell_ms = 350;
    t.log(c);

    MorphEvent d;
    d.kind = MorphEventKind::ABToggled;
    d.from_slot = 0;
    d.to_slot = 1;
    t.log(d);

    MorphEvent e;
    e.kind = MorphEventKind::PresetCommitted;
    e.name_length = 9;
    e.prompt_hash = MorphTelemetry::hashPrompt("first");
    t.log(e);

    MorphEvent f;
    f.kind = MorphEventKind::BounceToWav;
    f.duration_s = 4.0f;
    t.log(f);

    const auto lines = readLines(path);
    REQUIRE(lines.size() == 6);

    auto kindOf = [&](size_t i) {
        return juce::JSON::parse(juce::String(lines[i]))["kind"].toString().toStdString();
    };
    REQUIRE(kindOf(0) == "morph_requested");
    REQUIRE(kindOf(1) == "variation_picked");
    REQUIRE(kindOf(2) == "macro_tweaked");
    REQUIRE(kindOf(3) == "ab_toggled");
    REQUIRE(kindOf(4) == "preset_committed");
    REQUIRE(kindOf(5) == "bounce_to_wav");
}

TEST_CASE("MorphTelemetry: 10 concurrent record calls all land in the file", "[morph_telemetry]") {
    const auto path = makeTempPath("threaded");
    resetTelemetry(path);

    constexpr int kThreads = 10;
    constexpr int kPerThread = 50;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([i]() {
            for (int j = 0; j < kPerThread; ++j) {
                MorphEvent ev;
                ev.kind = MorphEventKind::MacroTweaked;
                ev.macro_index = i % 4;
                ev.value = static_cast<float>(j) / static_cast<float>(kPerThread);
                ev.dwell_ms = 100 + j;
                MorphTelemetry::instance().log(ev);
            }
        });
    }
    for (auto& w : workers)
        w.join();

    const auto lines = readLines(path);
    REQUIRE(lines.size() == static_cast<size_t>(kThreads * kPerThread));
}

TEST_CASE("MorphTelemetry: each line parses as valid JSONL", "[morph_telemetry]") {
    const auto path = makeTempPath("valid_jsonl");
    resetTelemetry(path);

    auto& t = MorphTelemetry::instance();
    for (int i = 0; i < 25; ++i) {
        MorphEvent ev;
        // Cycle through every kind so every code path in log() is exercised.
        switch (i % 6) {
        case 0:
            ev.kind = MorphEventKind::MorphRequested;
            ev.prompt_hash = MorphTelemetry::hashPrompt("p" + std::to_string(i));
            ev.history_size = i;
            break;
        case 1:
            ev.kind = MorphEventKind::VariationPicked;
            ev.strategy_id = i % 5;
            ev.label = "lab_" + std::to_string(i);
            ev.time_since_arrival_ms = 500 + i * 10;
            break;
        case 2:
            ev.kind = MorphEventKind::MacroTweaked;
            ev.macro_index = i % 4;
            ev.value = 0.5f;
            ev.dwell_ms = 200;
            break;
        case 3:
            ev.kind = MorphEventKind::ABToggled;
            ev.from_slot = 0;
            ev.to_slot = 1;
            break;
        case 4:
            ev.kind = MorphEventKind::PresetCommitted;
            ev.name_length = 12;
            ev.prompt_hash = MorphTelemetry::hashPrompt("prompt");
            break;
        case 5:
            ev.kind = MorphEventKind::BounceToWav;
            ev.duration_s = 4.0f;
            break;
        }
        t.log(ev);
    }

    const auto lines = readLines(path);
    REQUIRE(lines.size() == 25);
    for (const auto& line : lines) {
        auto parsed = juce::JSON::parse(juce::String(line));
        // juce::JSON::parse returns a default-constructed (void) var when the
        // input isn't a valid JSON value — a successfully parsed object
        // satisfies isObject().
        REQUIRE(parsed.isObject());
        REQUIRE(parsed.getDynamicObject() != nullptr);
        REQUIRE(parsed["ts"].toString().isNotEmpty());
        REQUIRE(parsed["kind"].toString().isNotEmpty());
    }
}

TEST_CASE("MorphTelemetry: prompt_hash is deterministic and 8 hex chars", "[morph_telemetry]") {
    const auto h1 = MorphTelemetry::hashPrompt("a warm pad");
    const auto h2 = MorphTelemetry::hashPrompt("a warm pad");
    const auto h3 = MorphTelemetry::hashPrompt("a different prompt");

    REQUIRE(h1.size() == 8);
    REQUIRE(h1 == h2);
    REQUIRE(h1 != h3);

    // First 8 hex chars of SHA-256("") — well-known test vector for empty
    // string is e3b0c442… → first 8 chars = "e3b0c442".
    REQUIRE(MorphTelemetry::hashPrompt("") == std::string{"e3b0c442"});
    // SHA-256("abc") = ba7816bf… → first 8 chars = "ba7816bf".
    REQUIRE(MorphTelemetry::hashPrompt("abc") == std::string{"ba7816bf"});
}

TEST_CASE("MorphTelemetry: disabled telemetry never writes", "[morph_telemetry]") {
    const auto path = makeTempPath("disabled");
    MorphTelemetry::instance().setPathForTest(path);
    MorphTelemetry::instance().setEnabled(false);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    MorphEvent ev;
    ev.kind = MorphEventKind::MorphRequested;
    ev.prompt_hash = MorphTelemetry::hashPrompt("ignored");
    MorphTelemetry::instance().log(ev);

    REQUIRE_FALSE(std::filesystem::exists(path));

    // Re-enable for downstream tests in this process.
    MorphTelemetry::instance().setEnabled(true);
}
