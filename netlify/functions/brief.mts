// ── POST /api/brief — Netlify v2 function (#280 / #309 / #311) ───────
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
// Request validation (#311): body size + validateBriefRequest before Gemini.

import {
  sanitizePrompt,
  validateBriefRequest,
} from "../../libs/prompt/src/index.ts";
import { enhanceBrief } from "./lib/gemini.mts";
import { BriefCache, canonicalKey } from "./lib/cache.mts";
import { gateRateLimit } from "./lib/rateLimitGate.mts";
import { readJsonObject } from "./lib/requestBody.mts";

export const config = { path: "/api/brief" };

const briefCache = new BriefCache();

function json(payload: unknown, status: number): Response {
  return new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

export type BriefHandlerDeps = {
  gateRateLimit?: typeof gateRateLimit;
  enhanceBrief?: typeof enhanceBrief;
  getApiKey?: () => string | undefined;
};

export async function handleBrief(
  req: Request,
  deps: BriefHandlerDeps = {},
): Promise<Response> {
  if (req.method !== "POST") {
    return json({ error: "Method not allowed. Use POST." }, 405);
  }

  try {
    const body = await readJsonObject(req);
    if (!body.ok) {
      return json({ error: body.error }, 400);
    }

    const promptGate = validateBriefRequest(body.value);
    if (!promptGate.ok) {
      return json({ error: promptGate.error }, 400);
    }
    const prompt = promptGate.prompt;

    // Rate limit BEFORE any upstream work (and before the key check, so
    // a missing-key deploy never hands out unlimited free probes).
    const gate = deps.gateRateLimit ?? gateRateLimit;
    const limited = await gate(req, "brief");
    if (limited !== null) return limited;

    const apiKey = (deps.getApiKey ?? (() => process.env.GEMINI_KEY))();
    if (typeof apiKey !== "string" || apiKey.length === 0) {
      return json({ error: "Generation is not configured (missing GEMINI_KEY)." }, 503);
    }

    const enhance = deps.enhanceBrief ?? enhanceBrief;
    const sanitized = sanitizePrompt(prompt);
    const cacheKey = canonicalKey(sanitized);
    let brief = briefCache.get(cacheKey);
    if (brief === undefined) {
      brief = await enhance(sanitized, apiKey);
      briefCache.set(cacheKey, brief);
    }
    return json({ brief }, 200);
  } catch {
    // Hostile input should already be 400 above; this is for unexpected
    // runtime failures only.
    return json({ error: "Internal server error." }, 500);
  }
}

export default async function handler(req: Request): Promise<Response> {
  return handleBrief(req);
}
