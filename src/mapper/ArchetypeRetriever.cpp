#include "mapper/ArchetypeRetriever.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace agentic_synth::mapper {

namespace {

std::string toLowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Score = number of tags that appear as substrings in `lowerPrompt`. We use
// raw substring instead of word-boundary matching so multi-word tags ("blade
// runner", "deep bass") work without bookkeeping — substrings are good
// enough for the Phase 34a keyword scorer.
int scoreArchetype(const Archetype& a, const std::string& lowerPrompt) {
    int score = 0;
    for (const auto& tag : a.tags) {
        if (tag.empty())
            continue;
        const std::string lowerTag = toLowerAscii(tag);
        if (lowerPrompt.find(lowerTag) != std::string::npos)
            ++score;
    }
    return score;
}

const Archetype* defaultFallback() {
    const Archetype* fb = ArchetypeLibrary::byName("default_init");
    if (fb)
        return fb;
    const auto& lib = ArchetypeLibrary::all();
    return lib.empty() ? nullptr : &lib.front();
}

} // namespace

const Archetype* ArchetypeRetriever::retrieve(const std::string& prompt) {
    const auto& lib = ArchetypeLibrary::all();
    if (lib.empty())
        return nullptr;

    const std::string lower = toLowerAscii(prompt);
    const Archetype* best = nullptr;
    int bestScore = 0;
    for (const auto& a : lib) {
        const int s = scoreArchetype(a, lower);
        if (s <= 0)
            continue;
        if (s > bestScore || (s == bestScore && best && a.name < best->name)) {
            bestScore = s;
            best = &a;
        }
    }
    if (!best)
        return defaultFallback();
    return best;
}

std::vector<const Archetype*> ArchetypeRetriever::retrieveTopN(const std::string& prompt, int n) {
    std::vector<const Archetype*> result;
    const auto& lib = ArchetypeLibrary::all();
    if (lib.empty() || n <= 0)
        return result;

    const std::string lower = toLowerAscii(prompt);

    // Build (score, ptr) pairs and sort descending. Tie-break alphabetical
    // by name so the order is deterministic.
    struct Entry {
        int score;
        const Archetype* a;
    };
    std::vector<Entry> entries;
    entries.reserve(lib.size());
    for (const auto& a : lib)
        entries.push_back({scoreArchetype(a, lower), &a});

    std::sort(entries.begin(), entries.end(), [](const Entry& l, const Entry& r) {
        if (l.score != r.score)
            return l.score > r.score;
        return l.a->name < r.a->name;
    });

    const int take = std::min<int>(n, static_cast<int>(entries.size()));
    result.reserve(static_cast<size_t>(take));
    for (int i = 0; i < take; ++i)
        result.push_back(entries[static_cast<size_t>(i)].a);
    return result;
}

} // namespace agentic_synth::mapper
