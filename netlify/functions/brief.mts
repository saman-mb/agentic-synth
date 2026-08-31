// ── POST /api/brief — Netlify v2 function (#280) ────────────────────
//
// First leg of the split pipeline: the enhancer (sound-design translator)
// alone. The platform kills a function at 10 s TOTAL on the free plan
// (#280 r9), so the enhancer (≈2 s on flash-lite) and the generator
// (≈3 s) run as two separate invocations, orchestrated by the browser.
//
// Graceful degradation (C++ parity): an enhancer failure returns the
// sanitized raw prompt as the brief — never a 500.

import { validatePrompt } from "../../libs/prompt/src/index.ts";
import { enhanceBrief, sanitizePrompt } from "./lib/gemini.mts";
import { BriefCache, canonicalKey } from "./lib/cache.mts";
import { checkRateLimit, createRateLimitStore } from "./lib/rateLimit.mts";

export const config = { path: "/api/brief" };

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
    const promptGate = validatePrompt((raw as Record<string, unknown>)["prompt"]);
    if (!promptGate.ok) {
      return json({ error: promptGate.error }, 400);
    }
    const prompt = promptGate.prompt;

    // Rate limit BEFORE any upstream work (and before the key check, so
    // a missing-key deploy never hands out unlimited free probes).
    const limit = checkRateLimit(rateStore, clientIp(req), Date.now());
    if (!limit.allowed) {
      return json({ error: "The demo is busy right now. Please wait a minute and try again." }, 429);
    }

    const apiKey = process.env.GEMINI_KEY;
    if (typeof apiKey !== "string" || apiKey.length === 0) {
      return json({ error: "Generation is not configured (missing GEMINI_KEY)." }, 503);
    }

    const sanitized = sanitizePrompt(prompt);
    const cacheKey = canonicalKey(sanitized);
    let brief = briefCache.get(cacheKey);
    if (brief === undefined) {
      brief = await enhanceBrief(sanitized, apiKey);
      briefCache.set(cacheKey, brief);
    }
    return json({ brief }, 200);
  } catch {
    return json({ error: "Internal server error." }, 500);
  }
}
