// ── Shared JSON body reader for Netlify handlers (#311) ─────────────
//
// Reject oversized bodies before JSON.parse / Gemini. Size SSOT lives in
// libs/prompt (MAX_REQUEST_BODY_BYTES).

import { MAX_REQUEST_BODY_BYTES } from "../../../libs/prompt/src/index.ts";

export { MAX_REQUEST_BODY_BYTES };

export type ReadJsonObjectResult =
  | { ok: true; value: Record<string, unknown> }
  | { ok: false; error: string };

const OVERSIZE =
  `Request body must be at most ${MAX_REQUEST_BODY_BYTES} bytes.`;

function utf8ByteLength(text: string): number {
  return new TextEncoder().encode(text).byteLength;
}

/**
 * Read + parse a JSON object from a Request, enforcing the shared body cap.
 * Never throws — hostile / truncated / huge inputs become `{ ok: false }`.
 */
export async function readJsonObject(req: Request): Promise<ReadJsonObjectResult> {
  const contentLength = req.headers.get("content-length");
  if (contentLength !== null) {
    const declared = Number(contentLength);
    if (!Number.isFinite(declared) || declared < 0) {
      return { ok: false, error: "Request body Content-Length is invalid." };
    }
    if (declared > MAX_REQUEST_BODY_BYTES) {
      return { ok: false, error: OVERSIZE };
    }
  }

  let text: string;
  try {
    text = await req.text();
  } catch {
    return { ok: false, error: "Request body could not be read." };
  }

  if (utf8ByteLength(text) > MAX_REQUEST_BODY_BYTES) {
    return { ok: false, error: OVERSIZE };
  }

  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return { ok: false, error: "Request body must be valid JSON." };
  }

  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) {
    return { ok: false, error: "Request body must be a JSON object." };
  }

  return { ok: true, value: raw as Record<string, unknown> };
}
