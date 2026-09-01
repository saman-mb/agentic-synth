// ── POST /api/brief — Netlify v2 function (#280 / #309) ─────────────
//
// First leg of the split pipeline: the enhancer (sound-design translator)
// alone. The platform kills a function at 10 s TTL on the free plan
// (#280 r9), so the enhancer (≈2 s on flash-lite) and the generator
// (≈3 s) run as two separate invocations, orchestrated by the browser.
//
// Graceful degradation (C++ parity): an enhancer failure returns the
// sanitized raw prompt as the brief — never a 500.
//
// Durable tiered rate limit (#309) runs before any Gemini work.

import { validatePrompt } from "../../libs/prompt/src/index.ts";
import { enhanceBrief, sanitizePrompt } from "./lib/gemini.mts";
import { BriefCache, canonicalKey } from "./lib/cache.mts";
import { gateRateLimit } from "./lib/rateLimitGate.mts";

export const config = { path: "/api/brief" };

const briefCache = new BriefCache();

function json(payload: unknown, status: number): Response {
  return new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json" },
  });
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
    const limited = await gateRateLimit(req, "brief");
    if (limited !== null) return limited;

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
