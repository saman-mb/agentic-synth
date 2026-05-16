#pragma once

#include <string>
#include <vector>

#include "engine/PatchStruct.h"

namespace agentic_synth::mapper {

// Phase 34a — minimum-viable RAG patch system.
//
// The AI Engineer panel (Phase 34) concluded that no published commercial AI
// synth reliably generates a full patch from text. The robust SOTA pattern is
// RAG over a curated archetype library + LLM as a delta-selector. Phase 34a
// ships that pattern at minimum scope:
//
//   1. A hand-curated set of ~15 archetype patches covering the major intent
//      space (cinematic pad, Vangelis pad, ambient drone, Reese, 808 sub,
//      acid 303, supersaw lead, DX7 tine, glass bell, warm pad, pluck, gritty
//      lead, choir pad, riser, default init).
//   2. Each archetype carries a `tags` array (strings) that the keyword
//      retriever scores against the user prompt.
//   3. The patch body matches the PatchStruct JSON shape that
//      GrammarSampler::parse_patch_json reads. The loader strips `tags` before
//      handing the JSON to that parser (PatchStruct itself has no tags field).
//
// The archetypes are stored as static C++ string literals (NOT loaded from
// disk) so they ship with the binary — there is nothing to deploy and nothing
// for an end-user to break by editing a file in the wrong place.
//
// Phase 34b (deferred) will replace the keyword-substring scorer with CLAP /
// sentence-transformer embeddings.

struct Archetype {
    std::string name;
    std::vector<std::string> tags;
    PatchStruct patch;
};

class ArchetypeLibrary {
public:
    // Returns the full list of loaded archetypes. Lazy-initialised on first
    // call; subsequent calls return the same reference. Thread-safe under
    // C++11 static-local-initialisation rules.
    [[nodiscard]] static const std::vector<Archetype>& all();

    // Test seam — look up an archetype by name. Returns nullptr when missing.
    [[nodiscard]] static const Archetype* byName(const std::string& name);
};

} // namespace agentic_synth::mapper
