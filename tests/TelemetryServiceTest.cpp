// Phase 10C verification — TelemetryService extracted from AgentBridge.
//
// Confirms that:
//   * setEnabled toggles the underlying Telemetry state.
//   * getTelemetryJson() emits a telemetry_data frame and reflects the
//     enabled flag.
//
// The service composes a real Telemetry; we point it at a temp path so the
// disable-time flush() doesn't pollute the user's telemetry dir.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "agent/TelemetryService.h"

using agentic_synth::agent::TelemetryService;

namespace {

std::string tempLogPath() {
    auto p = std::filesystem::temp_directory_path() / "agentic_synth_telemetry_service_test.json";
    return p.string();
}

} // namespace

TEST_CASE("TelemetryService: setEnabled toggles state and JSON reflects it") {
    const auto path = tempLogPath();
    std::filesystem::remove(path);

    TelemetryService svc{path};
    REQUIRE_FALSE(svc.isEnabled());

    svc.setEnabled(true);
    CHECK(svc.isEnabled());

    const std::string jsonOn = svc.getTelemetryJson();
    // Framing
    REQUIRE(jsonOn.find("\"type\":\"telemetry_data\"") != std::string::npos);
    // Enabled flag surfaces in the wrapped Telemetry::toJson() body.
    CHECK(jsonOn.find("\"enabled\":true") != std::string::npos);

    svc.setEnabled(false);
    CHECK_FALSE(svc.isEnabled());

    const std::string jsonOff = svc.getTelemetryJson();
    REQUIRE(jsonOff.find("\"type\":\"telemetry_data\"") != std::string::npos);
    CHECK(jsonOff.find("\"enabled\":false") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("TelemetryService: getTelemetryJson is well-formed before any records") {
    TelemetryService svc{tempLogPath()};
    const std::string json = svc.getTelemetryJson();
    // Must contain the frame type, summary block, and records array.
    REQUIRE(json.find("\"type\":\"telemetry_data\"") != std::string::npos);
    REQUIRE(json.find("\"summary\":") != std::string::npos);
    REQUIRE(json.find("\"records\":[") != std::string::npos);
}
