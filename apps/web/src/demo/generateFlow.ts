// ── Web-demo generate flow (issue #280) ─────────────────────────────
//
// Browser-side stand-in for WebUiComponent.cpp's generate job: POST the
// prompt to the relative /api/generate endpoint (Netlify function, no
// absolute URLs and no key material here) and replay the exact event
// sequence the C++ backend emits — enhancement → patch → patch_update →
// token(s) → rationale → done, or a single `error` event on any failure.
//
// The brief / rationale is re-sent as `token` frames (chunked for the
// chat ticker) mirroring the C++ behaviour of streaming the rationale as
// the primary bubble text (WebUiComponent.cpp notifyToken block). The
// click handler that invoked `generate` is the autoplay-safe user
// gesture, so ensureStarted() is kicked off at flow entry (idempotent in
// the engine); a failed audio start never aborts the event flow — the UI
// still shows the patch, and the note handlers lazy-start the engine.
//
// Response contract (owned by the /api/generate function builder):
//   200 { "brief": string, "patch": PatchParams, "rationale"?: string,
//         "modulation"?: AgentModulationPlan, "augmenter_actions"?: string[] }
//   400/429/502/503 { "error": string }

import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
import type { SynthEngine } from '../webaudio/engine';
import { validatePatch } from './patchCodec';

/** What the shim hands the flow — emit fans out to every JUCE listener. */
export interface GenerateFlowDeps {
  emit: (name: string, payload: unknown) => void;
  engine: SynthEngine;
  /** setPatch + applyMacros + update the shim's current-patch snapshot. */
  applyServerPatch: (patch: PatchParams, modulation?: AgentModulationPlan) => void;
}

interface GenerateApiResponse {
  brief?: unknown;
  // Optional patch display name — mirrored into the TopBar selector by
  // the App when present. The current function never sends it; kept for
  // wire forward-compatibility.
  name?: unknown;
  patch?: unknown;
  rationale?: unknown;
  modulation?: unknown;
  augmenter_actions?: unknown;
  error?: unknown;
}

// ── pure helpers (no I/O — unit-testable) ────────────────────────────

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null;
}

function asString(v: unknown): string | undefined {
  return typeof v === 'string' && v.length > 0 ? v : undefined;
}

/** Friendly, user-facing copy per HTTP status; detail appended when present. */
export function friendlyError(status: number, detail?: string): string {
  const suffix = detail ? ` (${detail})` : '';
  switch (status) {
    case 400: return `The prompt was rejected${suffix}. Try rephrasing it.`;
    case 429: return 'Rate limited (429) — too many generations in a row. Wait a moment and try again.';
    case 502: return 'Patch service unavailable (502) — the upstream model could not be reached.';
    case 503:
      return 'Generation is not configured on this server — set GEMINI_KEY and restart (local) or ask the site owner (deployed).';
    default: return `Generation failed (HTTP ${status}).${suffix}`;
  }
}

/** Split text into small word-group chunks so the ticker streams. */
export function chunkText(text: string, wordsPerChunk = 3): string[] {
  const words = text.split(/\s+/).filter(Boolean);
  const chunks: string[] = [];
  for (let i = 0; i < words.length; i += wordsPerChunk) {
    chunks.push(words.slice(i, i + wordsPerChunk).join(' '));
  }
  return chunks;
}

// ── the flow ─────────────────────────────────────────────────────────

// sessionId is accepted by the wire contract but unused: /api/generate
// correlates nothing server-side (stateless function call).
export async function runGenerateFlow(
  deps: GenerateFlowDeps,
  prompt: string,
): Promise<void> {
  const { emit } = deps;

  if (typeof prompt !== 'string' || prompt.trim().length === 0) {
    emit('error', { message: 'Type a prompt first — there is nothing to generate.' });
    return;
  }

  // Start the AudioContext while the generate click's user activation is
  // still fresh (a slow fetch can outlive the transient gesture window).
  // ensureStarted() is idempotent; the AudioContext starts (or resumes)
  // even when the fetch below fails — note handlers also lazy-start the
  // engine, so no readiness flag is needed anymore.
  void deps.engine.ensureStarted().catch(() => {
    // A failed start is surfaced by the note handlers' lazy-start path;
    // never abort the event flow here — the UI still shows the patch.
  });

  // ── Leg 1: /api/brief — the enhancer. Emits `enhancement` as soon as
  // the brief lands, matching the C++ ordering (notifyEnhancement fires
  // right after enhancePrompt, before the generator runs) and giving the
  // visitor something to read while the patch generates. Each endpoint
  // must stay under the platform's 10 s function cap (#280 r9), hence
  // the split.
  let briefRes: Response;
  try {
    briefRes = await fetch('/api/brief', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt }),
    });
  } catch {
    emit('error', { message: 'Could not reach the patch service — check your connection and try again.' });
    return;
  }

  let briefData: { brief?: string; error?: string } = {};
  try {
    briefData = JSON.parse(await briefRes.text()) as { brief?: string; error?: string };
  } catch {
    // Unparsable body — the status check below produces the error event.
  }

  if (!briefRes.ok) {
    emit('error', { message: friendlyError(briefRes.status, asString(briefData.error)) });
    return;
  }
  const serverBriefError = asString(briefData.error);
  if (serverBriefError) {
    emit('error', { message: serverBriefError });
    return;
  }
  const brief = asString(briefData.brief) ?? prompt.trim();
  emit('enhancement', { brief });

  // ── Leg 2: /api/generate — the patch generator, fed the brief.
  let res: Response;
  try {
    res = await fetch('/api/generate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt, brief }),
    });
  } catch {
    emit('error', { message: 'Could not reach the patch service — check your connection and try again.' });
    return;
  }

  let data: GenerateApiResponse = {};
  try {
    data = JSON.parse(await res.text()) as GenerateApiResponse;
  } catch {
    // Body unparsable — fall through with an empty object; the status
    // check below produces the error event.
  }

  if (!res.ok) {
    emit('error', { message: friendlyError(res.status, asString(data.error)) });
    return;
  }

  const serverError = asString(data.error);
  if (serverError) {
    // Defensive: a 200 body carrying {"error": ...} is a failure too.
    emit('error', { message: serverError });
    return;
  }

  // Defence in depth: the server already ran convertLlmPatch + validate;
  // never trust the wire. An invalid patch is a function error, exactly
  // like the C++ parser's fail-closed behaviour.
  const validation = validatePatch(data.patch);
  if (!validation.ok) {
    emit('error', { message: `The patch service returned an invalid patch (${validation.error}).` });
    return;
  }
  const patch = data.patch as PatchParams;

  // 1. Brief already emitted above (leg 1) — the patch frame comes next,
  //    matching the C++ order enhancement → patch → patch_update → ….

  // 2. Apply to the engine (setPatch then macros) and update the snapshot
  //    so knob_tweak / feedback / audition keys work against this patch.
  const modulation = isRecord(data.modulation) ? (data.modulation as AgentModulationPlan) : undefined;
  deps.applyServerPatch(patch, modulation);

  // 3. Patch frame BEFORE the rationale stream — C++ order is patch →
  //    patch_update → token(s) → rationale → done (WebUiComponent.cpp:468-525).
  const actions = Array.isArray(data.augmenter_actions)
    ? data.augmenter_actions.filter((a): a is string => typeof a === 'string')
    : [];
  const patchPayload: Record<string, unknown> = { variation: 'A', data: patch };
  if (modulation) patchPayload.modulation = modulation;
  if (actions.length > 0) patchPayload.augmenter_actions = actions;
  emit('patch', patchPayload);
  // patch_name is optional wire parity: the App mirrors it into the TopBar
  // selector when the service names the patch (the C++ backend ignores the
  // extra field). Absent → the UI falls back to its agent label.
  const patchName = asString(data.name);
  emit(
    'patch_update',
    patchName ? { patch, modulation, patch_name: patchName } : { patch, modulation },
  );

  // 4. Rationale re-streamed as token frames (C++ re-sends the rationale as
  //    the primary bubble text), then the single rationale frame the
  //    "Why this patch?" details read.
  const rationaleText = asString(data.rationale) ?? brief;
  for (const chunk of chunkText(rationaleText)) {
    emit('token', { content: chunk });
  }
  emit('rationale', { text: rationaleText });

  // 5. done — the UI stops the ticker and commits the bubble.
  emit('done', {});
}
