#pragma once

#include <string>

#include "engine/PatchStruct.h"

namespace agentic_synth::agent {

// Phase 23 — runtime guardrail that enforces system-prompt §0 rules 10/12 on
// LLM-generated patches when the model ignores them.
//
// The system prompt (src/mapper/system-prompt.md) instructs the LLM to default
// to a 3-oscillator architecture for any descriptive prompt and bans
// noise-only patches ("noise alone is a texture not a voice"). Empirically the
// model honours these rules ~80% of the time. The remaining 20% — patches
// that ship with osc[0] alone audible, or only Noise oscillators enabled —
// produce thin, missed-opportunity sounds that contradict the brief.
//
// augmentPatch() is the server-side safety net: it post-processes the parsed
// PatchStruct BEFORE the engine sees it, layering additional oscillators in
// the same family as the LLM's pick, and replacing noise-only patches with a
// pitched fundamental + noise as a texture layer.
//
// All augmentations are conservative: they only fire when the prompt is NOT
// explicitly minimal AND the patch is under-layered. The producer's stated
// topology (when they ask for "pure sine sub" or "single-osc lead") is always
// honoured.
//
// All mutations log to stderr with the chosen strategy + the originating
// prompt so producers know why the audible patch differs from the JSON the
// LLM emitted.
//
// Returns true if the patch was modified, false if untouched.
bool augmentPatch(PatchStruct& p, const std::string& prompt) noexcept;

// Returns true when the prompt explicitly asks for a minimal / single-osc /
// pure sound — exempts the patch from the 3-osc enforcement.
//
// Keyword sweep, case-insensitive substring, word-boundary aware for single
// tokens. Multi-word fragments ("just a", "one osc") encode their own
// boundaries.
bool isSimplePrompt(const std::string& prompt) noexcept;

} // namespace agentic_synth::agent
