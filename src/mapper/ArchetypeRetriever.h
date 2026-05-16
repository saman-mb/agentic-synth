#pragma once

#include <string>
#include <vector>

#include "mapper/ArchetypeLibrary.h"

namespace agentic_synth::mapper {

// Phase 34a — keyword retriever over the curated archetype library.
//
// Score each archetype by counting how many of its `tags` appear as
// case-insensitive substrings in the lower-cased prompt. The archetype with
// the highest score wins. Ties are broken by name alphabetical so the
// result is deterministic. When no archetype scores above zero we return the
// `default_init` archetype as a hard floor — that's still strictly better
// than shipping a make_default_patch baseline when the LLM stack is
// unreachable.
//
// Embeddings (CLAP / sentence-transformers) are explicitly out of scope —
// Phase 34b will replace this scorer.
class ArchetypeRetriever {
public:
    // Returns the best-matching archetype for `prompt`. Never returns
    // nullptr — falls back to the `default_init` archetype which is
    // guaranteed to be present in the library.
    [[nodiscard]] static const Archetype* retrieve(const std::string& prompt);

    // Returns the top-N archetypes by score, descending. When fewer than N
    // archetypes score above zero, the list is padded with the next-best
    // archetypes by alphabetical order so the caller always receives N
    // entries (or fewer if the library itself is smaller). Pass n=1 for the
    // same result as retrieve().
    [[nodiscard]] static std::vector<const Archetype*> retrieveTopN(const std::string& prompt, int n);
};

} // namespace agentic_synth::mapper
