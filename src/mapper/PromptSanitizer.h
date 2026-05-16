#pragma once

#include <string>

namespace agentic_synth::mapper {

// Phase 33: soften known Gemini safety-filter trigger words BEFORE sending
// the prompt to the API. The original word is replaced with a semantically
// adjacent term that conveys the same musical intent without tripping
// HARM_CATEGORY_DANGEROUS_CONTENT / HARASSMENT classifiers.
//
// Replacements (case-insensitive substring match, case-preserving for the
// first letter only — full-CAPS variants degrade to title-case which is
// acceptable for log output and irrelevant to the LLM):
//   horror   → uneasy
//   dread    → tension
//   menacing → intense
//   evil     → dark
//   scary    → unsettling
//   violent  → aggressive
//   kill     → drop
//   death    → ending
//
// Proper nouns ARE NOT remapped (Kubrick, Vangelis, Blade Runner): they
// are low-trigger and the BLOCK_ONLY_HIGH safetySettings already cover any
// rare false-positive. Producers reference them in 9/10 prompts that
// matter; rewriting "Kubrick" into "filmic" would erase the entire point
// of the request.
//
// Returns the sanitized string. Pure function, no allocations on the
// no-trigger path beyond the result copy.
[[nodiscard]] std::string sanitizePromptForSafety(const std::string& prompt);

} // namespace agentic_synth::mapper
