// ── Post-Gemini generate payload assembly (#311) ────────────────────
//
// Patch SSOT: decodeLlmPatch (libs/codec) — version accept-list, convert,
// validatePatch. Modulation SSOT: validateGeneratePayload (libs/modval),
// shared with the web client. No forked schema in netlify/.

import { decodeLlmPatch } from "../../../libs/codec/src/index.ts";
import { validateGeneratePayload } from "../../../libs/modval/src/index.ts";

// PatchStruct::rationale is char[256]; augmenter_actions entries follow
// the same cap (PatchAugmenter.cpp appends pipe-separated 256-char
// strings into a char[256] buffer).
const MAX_RATIONALE_LENGTH = 256;
const MAX_ACTION_LENGTH = 256;

function truncate(s: string, max: number): string {
  return s.length > max ? s.slice(0, max) : s;
}

export type AssembleGenerateResult =
  | { ok: true; payload: Record<string, unknown> }
  | { ok: false; status: 502; error: string };

/**
 * Parse model JSON → codec decode → modval gate → client payload.
 * Failures are intentional 502 (upstream model garbage), never 500.
 */
export function assembleGeneratePayload(
  modelText: string,
  promptForGenerator: string,
): AssembleGenerateResult {
  let parsedPatch: unknown;
  try {
    parsedPatch = JSON.parse(modelText);
  } catch {
    return { ok: false, status: 502, error: "patch: malformed JSON from model" };
  }

  // GrammarSampler parity: reject, never coerce. decodeLlmPatch is the
  // patch schema SSOT (#299 / #284 / #311).
  const decoded = decodeLlmPatch(parsedPatch);
  if (!decoded.ok) {
    const msg = decoded.error.message;
    const error =
      decoded.error.code === "unknown_version" || msg.startsWith("patch:")
        ? msg
        : `patch: ${msg}`;
    return { ok: false, status: 502, error };
  }

  // Defence in depth + optional modulation: same gate the web client uses
  // (#301). decodeLlmPatch already validated the patch; this re-checks and
  // accepts/rejects a side-car `modulation` if the model emitted one
  // (system prompt says not to — still validate hostile/out-of-spec).
  const llm = parsedPatch as Record<string, unknown>;
  const gated = validateGeneratePayload(decoded.patch, llm["modulation"]);
  if (!gated.ok) {
    return { ok: false, status: 502, error: gated.error };
  }

  const payload: Record<string, unknown> = {
    brief: promptForGenerator,
    patch: gated.patch,
  };
  if (gated.modulation !== undefined) {
    payload["modulation"] = gated.modulation;
  }

  const rationale = llm["rationale"];
  if (typeof rationale === "string" && rationale.length > 0) {
    payload["rationale"] = truncate(rationale, MAX_RATIONALE_LENGTH);
  }
  const actions = llm["augmenter_actions"];
  if (typeof actions === "string" && actions.length > 0) {
    payload["augmenter_actions"] = actions
      .split("|")
      .filter((a) => a.length > 0)
      .map((a) => truncate(a, MAX_ACTION_LENGTH));
  }

  return { ok: true, payload };
}
