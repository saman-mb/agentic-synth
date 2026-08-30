// ── POST /api/generate — Netlify v2 function (#280) ─────────────────
//
// Replicates the C++ Gemini path: sanitize → brief → patch JSON →
// validate/convert via the shared codec (ui/src/demo/patchCodec).
// Free-tier safeguards: in-memory rate limit (rateLimit.mts) + brief
// LRU cache (cache.mts). Node 20+ globals only, no external deps.

import { convertLlmPatch, validatePatch, type LlmPatch } from "../../ui/src/demo/patchCodec";
import { enhanceBrief, generatePatchText, sanitizePrompt } from "./lib/gemini.mts";
import { BriefCache, canonicalKey } from "./lib/cache.mts";
import { checkRateLimit, createRateLimitStore } from "./lib/rateLimit.mts";

export const config = { path: "/api/generate" };

const MAX_PROMPT_LENGTH = 2000;
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

// Per-isolate singletons — reset on cold start (see rateLimit.mts for
// the cold-start semantics this implies).
const rateStore = createRateLimitStore();
const briefCache = new BriefCache();

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

interface BodyError {
  error: string;
}

function parseBody(raw: string): { prompt: string; patchId: number } | BodyError {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return { error: "Request body must be valid JSON." };
  }
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    return { error: "Request body must be a JSON object." };
  }
  const obj = parsed as Record<string, unknown>;
  const prompt = obj["prompt"];
  if (typeof prompt !== "string" || prompt.trim().length === 0) {
    return { error: "prompt must be a non-empty string." };
  }
  if (prompt.length > MAX_PROMPT_LENGTH) {
    return { error: `prompt must be at most ${MAX_PROMPT_LENGTH} characters.` };
  }
  let patchId = 0;
  const rawPatchId = obj["patch_id"];
  if (rawPatchId !== undefined) {
    if (typeof rawPatchId !== "number" || !Number.isInteger(rawPatchId) || rawPatchId < 0) {
      return { error: "patch_id must be an integer >= 0." };
    }
    patchId = rawPatchId;
  }
  return { prompt, patchId };
}

function truncate(s: string, max: number): string {
  return s.length > max ? s.slice(0, max) : s;
}

export default async function handler(req: Request): Promise<Response> {
  if (req.method !== "POST") {
    return json({ error: "Method not allowed. Use POST." }, 405);
  }

  try {
    const body = parseBody(await req.text());
    if ("error" in body) return json(body, 400);

    // Rate limit BEFORE any upstream work (and before the key check, so
    // a missing-key deploy never hands out unlimited free probes).
    const ip = clientIp(req);
    const limit = checkRateLimit(rateStore, ip);
    if (!limit.allowed) {
      return json({ error: limit.window === "day" ? DAY_429 : MINUTE_429 }, 429);
    }

    // Immediate 503: never retried, never crashes, key never logged.
    const apiKey = process.env.GEMINI_KEY;
    if (typeof apiKey !== "string" || apiKey.length === 0) {
      return json({ error: "Generation is not configured (missing GEMINI_KEY)." }, 503);
    }

    // Sanitize once; the brief and the generator prompt both use it.
    const sanitized = sanitizePrompt(body.prompt);

    // Brief with LRU cache (hit = 1 upstream call saved).
    const cacheKey = canonicalKey(sanitized);
    let brief = briefCache.get(cacheKey);
    if (brief === undefined) {
      brief = await enhanceBrief(sanitized, apiKey);
      briefCache.set(cacheKey, brief);
    }

    // Generator: retries/backoff/deadline live inside gemini.mts.
    const generated = await generatePatchText(sanitized, body.patchId, apiKey);
    if (!generated.ok) {
      if (generated.reason === "blocked") return json({ error: SAFETY_400 }, 400);
      return json({ error: UPSTREAM_502 }, 502);
    }

    // GrammarSampler parity: reject, never coerce. Malformed model JSON
    // is a 400 after retries. The codec contract is convert-then-validate:
    // the LLM emits string enums, convertLlmPatch maps them to ints (an
    // unknown name becomes NaN), and validatePatch rejects the converted
    // patch. Structural garbage that makes conversion throw is 400 too.
    let parsedPatch: unknown;
    try {
      parsedPatch = JSON.parse(generated.text);
    } catch {
      return json({ error: "patch: malformed JSON from model" }, 400);
    }

    let patch: ReturnType<typeof convertLlmPatch>;
    try {
      patch = convertLlmPatch(parsedPatch as LlmPatch);
    } catch {
      return json({ error: "patch: malformed patch structure" }, 400);
    }

    const verdict = validatePatch(patch);
    if (!verdict.ok) {
      return json({ error: `patch: ${verdict.error}` }, 400);
    }

    // rationale / augmenter_actions are LLM-transport fields read off the
    // raw JSON (the codec drops them during conversion). Include only
    // when present, capped to the PatchStruct buffer conventions.
    const payload: Record<string, unknown> = { brief, patch };
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
