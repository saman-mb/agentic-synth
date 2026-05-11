#pragma once

#include <string>
#include <utility>

#include "agent/Telemetry.h"

namespace agentic_synth::agent {

// Phase 10C: extracted from AgentBridge to address god-class concerns.
//
// Single responsibility: public-facing telemetry surface. Wraps the engine-
// owned Telemetry object so AgentBridge no longer carries the JSON-framing,
// enable/disable, and flush logic inline.
//
// Composition (not inheritance): AgentBridge owns one TelemetryService which
// in turn owns the underlying Telemetry. Existing accessor AgentBridge::
// telemetry() returns a reference to the underlying engine object so call
// sites that need the raw Telemetry (UI events, GenerationService::
// recordGeneration, etc.) keep working unchanged.
//
// No audio-thread state. setEnabled / getJson / record* run on the UI /
// control thread; recordUiEvent on the Telemetry side already guards its
// own mutex.
class TelemetryService {
public:
    explicit TelemetryService(std::string log_path) : telemetry_(std::move(log_path)) {}

    // Telemetry serialised as a telemetry_data JSON frame.
    [[nodiscard]] std::string getTelemetryJson() const {
        // Telemetry::toJson() returns "{...}"; splice in the framing type.
        return "{\"type\":\"telemetry_data\"," + telemetry_.toJson().substr(1);
    }

    // Toggle telemetry on / off. Flushes pending records when disabling so
    // the on-disk log captures the final session state.
    void setEnabled(bool on) {
        telemetry_.setEnabled(on);
        if (!on)
            telemetry_.flush();
    }

    [[nodiscard]] bool isEnabled() const noexcept { return telemetry_.isEnabled(); }

    // Expose the wrapped Telemetry for code paths (GenerationService,
    // WebView lifecycle) that need to call recordGeneration / recordUiEvent
    // directly. Returning a reference keeps the existing AgentBridge::
    // telemetry() public API signature identical.
    [[nodiscard]] Telemetry& telemetry() noexcept { return telemetry_; }
    [[nodiscard]] const Telemetry& telemetry() const noexcept { return telemetry_; }

private:
    Telemetry telemetry_;
};

} // namespace agentic_synth::agent
