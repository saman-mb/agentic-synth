#pragma once

// Phase 9C: single source of truth for "UI param path → PatchDelta field".
//
// Replaces the long if/else cascade that previously lived inline in
// AgentBridge::paramToDelta. Adding or renaming a parameter is now a
// one-line edit in ParamMap.cpp instead of a hunt through five files.
//
// The table is intentionally a flat array of (path, assign-lambda) rows
// rather than an unordered_map: with ~25 entries linear scan beats hash
// + heap, and the resulting binary has zero static initialisers.

#include <span>
#include <string>
#include <string_view>

#include "mapper/descriptor_dataset.h" // mapper::PatchDelta

namespace agentic_synth::agent {

// One row in the parameter table.
//
// `assign` is a stateless function pointer (NOT a std::function) so the
// table can live in `constexpr` storage with no heap and no captures.
// Int-typed PatchDelta fields (e.g. voice_count, currently unused by
// paramToDelta) cast from the float `v` inside the lambda body.
struct ParamSlot {
    std::string_view path;
    void (*assign)(mapper::PatchDelta&, float);
};

// Returns the static parameter table. Stable for the lifetime of the
// process; safe to call from any thread.
[[nodiscard]] std::span<const ParamSlot> getParamMap() noexcept;

// Map a UI param path to a PatchDelta with that one field set; all other
// fields stay as nullopt. Unknown paths return a default-constructed
// PatchDelta (no-op) — same contract as the prior in-place if/else
// cascade in AgentBridge::paramToDelta.
[[nodiscard]] mapper::PatchDelta paramToDelta(const std::string& param, float value) noexcept;

} // namespace agentic_synth::agent
