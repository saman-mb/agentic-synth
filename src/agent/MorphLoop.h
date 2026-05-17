#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/PatchStruct.h"

namespace agentic_synth::agent {

// MorphLoop orchestrates 5 variation strategies producing a generation of
// candidate patches for the user to pick from. Distinct from
// engine::MorphEngine (Issue #86 — MIDI-CC live-morph between 4 patch
// targets) — same word, completely different concept.
//
// Phase B / TIMBRE simple-view (#249 / #250 / #263).
struct MorphResult {
    std::array<PatchStruct, 5> variations;
    std::array<std::string, 5> labels;
};

// Produce 5 variations from a base patch + history + liked pool.
// Strategy mix:
//   1. Heavy mutation (10-15% perturbation across all params)
//   2. Light mutation (3-5% perturbation, brand-coherent)
//   3. Crossover with most recent liked patch (lerp t=0.5)
//   4. Crossover with most recent history patch (lerp t=0.5)
//   5. Archetype bounce — pull from ArchetypeRetriever top-3
// Deterministic given the same (base, history, liked, prompt, seed).
MorphResult morph(const PatchStruct& base,
                  const std::vector<PatchStruct>& history,
                  const std::vector<PatchStruct>& liked,
                  const std::string& prompt,
                  uint32_t seed);

} // namespace agentic_synth::agent
