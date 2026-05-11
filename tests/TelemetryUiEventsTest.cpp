// Issue #91 / SRE P1: structured WebView lifecycle events feed the same
// opt-in Telemetry sink as generation records. These tests pin the public
// API around recordUiEvent + the ring-buffer cap so the WebUiComponent
// wiring (separate task) has a stable contract to build against.

#include "agent/Telemetry.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <process.h>
static int test_pid() noexcept { return static_cast<int>(::_getpid()); }
#else
#include <unistd.h>
static int test_pid() noexcept { return static_cast<int>(::getpid()); }
#endif

using agentic_synth::agent::Telemetry;
using agentic_synth::agent::UiEvent;

namespace {

std::string tempLogPath(const std::string& tag) {
    auto p = std::filesystem::temp_directory_path() /
             ("agentic_synth_telemetry_ui_" + tag + "_" + std::to_string(test_pid()) + ".json");
    return p.string();
}

} // namespace

TEST_CASE("Telemetry records UI events when enabled", "[Telemetry][UiEvents]") {
    Telemetry t(tempLogPath("basic"));
    t.setEnabled(true);

    const std::string url = "https://agenticsynth.local/index.html";
    t.recordUiEvent("page_about_to_load", url);
    t.recordUiEvent("page_about_to_load", url);
    t.recordUiEvent("page_about_to_load", url);

    auto events = t.uiEvents();
    REQUIRE(events.size() == 3);
    for (const auto& e : events) {
        REQUIRE(e.kind == "page_about_to_load");
        REQUIRE(e.detail == url);
        REQUIRE(e.ts_ms > 0);
    }
}

TEST_CASE("Telemetry drops UI events when disabled", "[Telemetry][UiEvents]") {
    Telemetry t(tempLogPath("disabled"));
    // setEnabled defaults to false; be explicit for the reader.
    t.setEnabled(false);

    t.recordUiEvent("page_about_to_load", "https://x/");
    t.recordUiEvent("page_load_error", "net::ERR_CONNECTION_REFUSED");

    REQUIRE(t.uiEvents().empty());

    // And re-enabling must not retroactively resurrect dropped events.
    t.setEnabled(true);
    REQUIRE(t.uiEvents().empty());

    t.recordUiEvent("page_finished_loading", "https://x/");
    REQUIRE(t.uiEvents().size() == 1);
}

TEST_CASE("Telemetry UI-event ring buffer caps at 256 and drops oldest", "[Telemetry][UiEvents]") {
    Telemetry t(tempLogPath("ringbuf"));
    t.setEnabled(true);

    for (int i = 0; i < 300; ++i) {
        t.recordUiEvent("page_finished_loading", "url-" + std::to_string(i));
    }

    auto events = t.uiEvents();
    REQUIRE(events.size() == Telemetry::kUiEventCap);
    REQUIRE(events.size() == 256);

    // Oldest 44 (300 - 256) should have been dropped: first surviving entry is url-44.
    REQUIRE(events.front().detail == "url-44");
    REQUIRE(events.back().detail == "url-299");
}

TEST_CASE("Telemetry toJson exposes ui_events array", "[Telemetry][UiEvents]") {
    Telemetry t(tempLogPath("json"));
    t.setEnabled(true);
    t.recordUiEvent("page_load_error", "net::ERR_FAILED");

    const auto json = t.toJson();
    // Loose contract — we don't pull in a JSON lib for tests. The wired UI
    // dashboard consumes this through the existing get_telemetry path.
    REQUIRE(json.find("\"ui_events\":[") != std::string::npos);
    REQUIRE(json.find("\"kind\":\"page_load_error\"") != std::string::npos);
    REQUIRE(json.find("\"detail\":\"net::ERR_FAILED\"") != std::string::npos);
}

// Regression: when a DAW hosts multiple plugin instances in one process,
// each AgentBridge constructs its own Telemetry via defaultLogPath(). Without
// a per-instance suffix both Telemetries would share `<dir>/telemetry_<pid>.json`,
// and their flush() writes would interleave and corrupt the JSON.
TEST_CASE("Telemetry::defaultLogPath returns unique paths per call", "[Telemetry][PathCollision]") {
    const auto p1 = Telemetry::defaultLogPath();
    const auto p2 = Telemetry::defaultLogPath();
    const auto p3 = Telemetry::defaultLogPath();

    REQUIRE_FALSE(p1.empty());
    REQUIRE(p1 != p2);
    REQUIRE(p2 != p3);
    REQUIRE(p1 != p3);

    // All three must still encode the current PID so on-disk grouping by
    // process is preserved (existing operator expectation).
    const auto pid_tag = "_" + std::to_string(test_pid()) + "_";
    REQUIRE(p1.find(pid_tag) != std::string::npos);
    REQUIRE(p2.find(pid_tag) != std::string::npos);
    REQUIRE(p3.find(pid_tag) != std::string::npos);
}

TEST_CASE("Telemetry instances constructed back-to-back have distinct log paths", "[Telemetry][PathCollision]") {
    // Mirrors AgentBridge's `Telemetry telemetry_{Telemetry::defaultLogPath()}`
    // default member init — two AgentBridges in the same process must not
    // collide.
    Telemetry a(Telemetry::defaultLogPath());
    Telemetry b(Telemetry::defaultLogPath());

    REQUIRE_FALSE(a.logPath().empty());
    REQUIRE_FALSE(b.logPath().empty());
    REQUIRE(a.logPath() != b.logPath());
}

TEST_CASE("Telemetry::defaultLogPath(index) is stable for a given index", "[Telemetry][PathCollision]") {
    // The explicit-index overload is the deterministic escape hatch (tests,
    // pinned slots). Same index → same path; different indices → different.
    REQUIRE(Telemetry::defaultLogPath(42) == Telemetry::defaultLogPath(42));
    REQUIRE(Telemetry::defaultLogPath(42) != Telemetry::defaultLogPath(43));
}
