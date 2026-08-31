// ── POST /api/generate — Netlify v2 function (#280) ─────────────────
//
// Second leg of the split pipeline: the patch generator alone. The
// platform kills a function at 10 s TOTAL on the free plan (#280 r9),
// so the enhancer runs in /api/brief and hands its brief to the browser,
// which posts it here. Generator ≈3 s on flash-lite; retries are slim
// (2 attempts inside a 9 s wall — see lib/gemini.mts).
//
// GrammarSampler parity: reject, never coerce (via the shared codec,
// libs/codec). Free-tier safeguards: in-memory per-IP rate
// limit + the brief cache lives in brief.mts.

import { convertLlmPatch, validatePatch, type LlmPatch } from "../../libs/codec/src/index.ts";
import { generatePatchText, sanitizePrompt } from "./lib/gemini.mts";
import { checkRateLimit, createRateLimitStore } from "./lib/rateLimit.mts";

export const config = { path: "/api/generate" };

const MAX_PROMPT_LENGTH = 2000;
const MAX_BRIEF_LENGTH = 4000;
// PatchStruct::rationale is char[256]; augmenter_actions entries follow
// the same cap (PatchAugmenter.cpp appends pipe-separated 256-char
// strings into a char[256] buffer).
const MAX_RATIONALE_LENGTH = 256;
const MAX_ACTION_LENGTH = 256;

const MINUTE_429 = "The demo is busy right now. Please wait a minute and try again.";
const DAY_429 =
  "You have reached the daily generation limit for the demo. Please come back tomorrow (UTC).";
const UPSTREAM_502 = "Upstream model request failed.";
const SAFETY_400 = "This prompt was blocked by safety filters. Try rewording it.";

const rateStore = createRateLimitStore();

function json(payload: unknown, status: number): Response {
  return new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function clientIp(req: Request): string {
  // Netlify injects x-nf-client-connection-ip at the edge; it is not
  // client-controllable. x-forwarded-for is only a fallback and its first
  // entry can be spoofed, so the limiter is a soft guardrail either way.
  const nf = req.headers.get("x-nf-client-connection-ip");
  if (nf !== null && nf.trim().length > 0) return nf.trim();
  const fwd = req.headers.get("x-forwarded-for");
  if (fwd !== null) {
    const first = fwd.split(",")[0].trim();
    if (first.length > 0) return first;
  }
  return "unknown";
}

function truncate(s: string, max: number): string {
  return s.length > max ? s.slice(0, max) : s;
}

export default async function handler(req: Request): Promise<Response> {
  if (req.method !== "POST") {
    return json({ error: "Method not allowed. Use POST." }, 405);
  }

  try {
    let raw: unknown;
    try {
      raw = JSON.parse(await req.text());
    } catch {
      return json({ error: "Request body must be valid JSON." }, 400);
    }
    if (typeof raw !== "object" || raw === null || Array.isArray(raw)) {
      return json({ error: "Request body must be a JSON object." }, 400);
    }
    const obj = raw as Record<string, unknown>;
    const prompt = obj["prompt"];
    if (typeof prompt !== "string" || prompt.trim().length === 0) {
      return json({ error: "prompt must be a non-empty string." }, 400);
    }
    if (prompt.length > MAX_PROMPT_LENGTH) {
      return json({ error: `prompt must be at most ${MAX_PROMPT_LENGTH} characters.` }, 400);
    }
    let patchId = 0;
    const rawPatchId = obj["patch_id"];
    if (rawPatchId !== undefined) {
      if (typeof rawPatchId !== "number" || !Number.isInteger(rawPatchId) || rawPatchId < 0) {
        return json({ error: "patch_id must be an integer >= 0." }, 400);
      }
      patchId = rawPatchId;
    }
    // The brief is client-supplied (from /api/brief) — it feeds the
    // generator prompt, so sanitize + cap it like any other text.
    let briefArg = "";
    const rawBrief = obj["brief"];
    if (typeof rawBrief === "string" && rawBrief.trim().length > 0) {
      briefArg = truncate(sanitizePrompt(rawBrief), MAX_BRIEF_LENGTH);
    }

    // Rate limit BEFORE any upstream work (and before the key check, so
    // a missing-key deploy never hands out unlimited free probes).
    const limit = checkRateLimit(rateStore, clientIp(req), Date.now());
    if (!limit.allowed) {
      return json({ error: limit.window === "day" ? DAY_429 : MINUTE_429 }, 429);
    }

    const apiKey = process.env.GEMINI_KEY;
    if (typeof apiKey !== "string" || apiKey.length === 0) {
      return json({ error: "Generation is not configured (missing GEMINI_KEY)." }, 503);
    }

    // C++ parity: the generator receives the sonic brief when one exists,
    // otherwise the sanitized raw prompt.
    const sanitized = sanitizePrompt(prompt);
    const promptForGenerator = briefArg.length > 0 ? briefArg : sanitized;

    const generated = await generatePatchText(promptForGenerator, patchId, apiKey);
    if (!generated.ok) {
      if (generated.reason === "blocked") return json({ error: SAFETY_400 }, 400);
      return json({ error: UPSTREAM_502 }, 502);
    }

    // GrammarSampler parity: reject, never coerce. Malformed model JSON
    // is an error after retries. The codec contract is convert-then-
    // validate: the LLM emits string enums, convertLlmPatch maps them
    // to ints (an unknown name becomes NaN), and validatePatch rejects
    // the converted patch. Structural garbage that makes conversion
    // throw is an error too.
    let parsedPatch: unknown;
    try {
      parsedPatch = JSON.parse(generated.text);
    } catch {
      return json({ error: "patch: malformed JSON from model" }, 502);
    }

    let patch: ReturnType<typeof convertLlmPatch>;
    try {
      patch = convertLlmPatch(parsedPatch as LlmPatch);
    } catch {
      return json({ error: "patch: malformed patch structure" }, 502);
    }

    const verdict = validatePatch(patch);
    if (!verdict.ok) {
      return json({ error: `patch: ${verdict.error}` }, 502);
    }

    // rationale / augmenter_actions are LLM-transport fields read off the
    // raw JSON (the codec drops them during conversion). Include only
    // when present, capped to the PatchStruct buffer conventions.
    const payload: Record<string, unknown> = { brief: promptForGenerator, patch };
    const llm = parsedPatch as Record<string, unknown>;
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
    return json(payload, 200);
  } catch {
    return json({ error: "Internal server error." }, 500);
  }
}
