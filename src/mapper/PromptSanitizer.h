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

// Phase C failure-state UX (#269) — variant that ALSO returns a human-
// readable diff string when at least one replacement fired. `outDiff`
// receives a single-line summary like "horror → uneasy, evil → dark"
// suitable for the FailureBanner detail disclosure. When no replacements
// fired, `outDiff` is set to the empty string and `prompt` is returned
// verbatim. Caller passes ownership of `outDiff`; we overwrite it.
[[nodiscard]] std::string sanitizePromptForSafetyWithDiff(const std::string& prompt,
                                                          std::string& outDiff);

// Phase C failure-state UX (#269) — process-wide queue of recent
// sanitizer modifications. Producers (GeminiSampler, PromptEnhancer)
// push via the auto-queueing wrapper inside `sanitizePromptForSafety`;
// PromptHandler pops the most recent entry after a generation completes
// and forwards it to the UI as a `safety_block` failure event.
//
// Implementation detail: a small bounded-size FIFO behind a mutex. We
// intentionally do not key by thread — generation may hop between the
// JUCE worker pool and the Gemini HTTP thread. The last-write-wins
// semantics are fine because the only legitimate consumer
// (PromptHandler::generateLlmPatch) calls pop right after its sampler
// call returns, so contention is negligible.
void pushSanitizerLog(std::string diff);
[[nodiscard]] std::string popSanitizerLog();

} // namespace agentic_synth::mapper
