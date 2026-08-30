// ── POST /api/generate — Netlify v2 function (#280) ─────────────────
//
// Replicates the C++ Gemini path: sanitize → brief → patch JSON →
// validate/convert via the shared codec (ui/src/demo/patchCodec).
// Free-tier safeguards: in-memory rate limit (rateLimit.mts) + brief
// LRU cache (cache.mts). Node 20+ globals only, no external deps.
//
// Streaming (#280 r8): Netlify's free plan kills non-streaming functions
// at 10 s time-to-first-byte, but the pipeline can legitimately run
// ~40 s (enhancer + generator retries). The handler therefore returns a
// ReadableStream that flushes its first byte immediately; the HTTP
// status is fixed at Response construction, so only pre-pipeline
// failures keep their real status (503) — failures detected mid-stream
// are 200 + {"error": ...}, which generateFlow's defensive branch
// treats as a failure.

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

// Streaming scaffold: send headers + a first byte immediately, run the
// pipeline, then enqueue exactly one final JSON document and close.
//
// The initial chunk is a single ASCII space: JSON.parse (and
// response.json()) skip leading whitespace, so the body stays one valid
// JSON document and generateFlow's res.text() + JSON.parse contract is
// unchanged. `run` returns the final body; its known-failure branches
// return JSON.stringify({error: ...}) — the client reads {error} on
// both !res.ok and 200 responses (generateFlow.ts).
function streamPipeline(status: number, run: () => Promise<string>): Response {
  const encoder = new TextEncoder();
  const stream = new ReadableStream<Uint8Array>({
    async start(controller) {
      const send = (s: string): void => {
        controller.enqueue(encoder.encode(s));
      };
      try {
        send(" "); // TTFB chunk — must land well inside the 10 s gate.
        send(await run());
      } catch {
        try {
          send(JSON.stringify({ error: "Internal server error." }));
        } catch {
          // Stream already closed or cancelled by the client.
        }
      } finally {
        try {
          controller.close();
        } catch {
          // Already closed or errored.
        }
      }
    },
  });
  return new Response(stream, {
    status,
    headers: {
      "Content-Type": "application/json",
      // Belt-and-braces: keep the edge from buffering the stream.
      "X-Accel-Buffering": "no",
    },
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
    // Knowable before the pipeline starts, so the streamed Response can
    // carry the real 503 status.
    const apiKey = process.env.GEMINI_KEY;
    if (typeof apiKey !== "string" || apiKey.length === 0) {
      return streamPipeline(503, async () =>
        JSON.stringify({ error: "Generation is not configured (missing GEMINI_KEY)." }),
      );
    }

    // Everything from here on runs after the headers have been sent, so
    // mid-pipeline failures cannot change the status; they return
    // {error} bodies that the client surfaces via its 200 + error branch.
    // Sanitize once; the brief and the generator prompt both use it.
    const sanitized = sanitizePrompt(body.prompt);

    return streamPipeline(200, async () => {
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
        if (generated.reason === "blocked") return JSON.stringify({ error: SAFETY_400 });
        return JSON.stringify({ error: UPSTREAM_502 });
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
        return JSON.stringify({ error: "patch: malformed JSON from model" });
      }

      let patch: ReturnType<typeof convertLlmPatch>;
      try {
        patch = convertLlmPatch(parsedPatch as LlmPatch);
      } catch {
        return JSON.stringify({ error: "patch: malformed patch structure" });
      }

      const verdict = validatePatch(patch);
      if (!verdict.ok) {
        return JSON.stringify({ error: `patch: ${verdict.error}` });
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
      return JSON.stringify(payload);
    });
  } catch {
    return json({ error: "Internal server error." }, 500);
  }
}
