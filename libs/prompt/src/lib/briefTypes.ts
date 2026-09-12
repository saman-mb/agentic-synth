// Pure brief request/response shapes + parse helpers (#300).
// Gemini HTTP transport stays in netlify/functions — this lib is DOM-free.

export interface BriefRequestBody {
  prompt: string;
}

export interface BriefResponseBody {
  brief: string;
}

export type BriefParseResult =
  | { ok: true; brief: string }
  | { ok: false; error: string };

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/** Parse a successful /api/brief JSON body (no network). */
export function parseBriefResponse(raw: unknown): BriefParseResult {
  if (!isRecord(raw)) {
    return { ok: false, error: 'brief response: expected object' };
  }
  if (typeof raw.error === 'string' && raw.error.length > 0) {
    return { ok: false, error: raw.error };
  }
  if (typeof raw.brief !== 'string' || raw.brief.trim().length === 0) {
    return { ok: false, error: 'brief response: missing brief string' };
  }
  return { ok: true, brief: raw.brief };
}

/** Build the JSON body for POST /api/brief after validatePrompt succeeds. */
export function briefRequestBody(prompt: string): BriefRequestBody {
  return { prompt };
}
