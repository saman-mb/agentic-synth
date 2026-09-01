// ── POST /api/generate — Netlify v2 function (#280 / #309–#311) ──────
//
// Second leg of the split pipeline: the patch generator alone. The
// platform kills a function at 10 s TTL on the free plan (#280 r9),
// so the enhancer runs in /api/brief and hands its brief to the browser,
// which posts it here. Generator ≈3 s on flash-lite; retries are slim
// (2 attempts inside a 9 s wall — see lib/gemini.mts).
//
// GrammarSampler parity: reject, never coerce (via the shared codec,
// libs/codec). Durable tiered rate limit (#309) runs before Gemini.
// Global daily quota (#310) after rate-limit allow, before Gemini.
// Request validation (#311): body size + validateGenerateRequest before
// Gemini; assembleGeneratePayload (codec + modval) after.

import {
  sanitizePrompt,
  validateGenerateRequest,
} from "../../libs/prompt/src/index.ts";
import { generatePatchText } from "./lib/gemini.mts";
import { gateQuota, noteOutcome } from "./lib/quotaGate.mts";
import { gateRateLimit } from "./lib/rateLimitGate.mts";
import { readJsonObject } from "./lib/requestBody.mts";
import { assembleGeneratePayload } from "./lib/assembleGeneratePayload.mts";

export const config = { path: "/api/generate" };

const MAX_BRIEF_LENGTH = 4000;

const UPSTREAM_502 = "Upstream model request failed.";
const SAFETY_400 = "This prompt was blocked by safety filters. Try rewording it.";

function json(payload: unknown, status: number): Response {
  return new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function truncate(s: string, max: number): string {
  return s.length > max ? s.slice(0, max) : s;
}

export type GenerateHandlerDeps = {
  gateRateLimit?: typeof gateRateLimit;
  gateQuota?: typeof gateQuota;
  noteOutcome?: typeof noteOutcome;
  generatePatchText?: typeof generatePatchText;
  getApiKey?: () => string | undefined;
};

export async function handleGenerate(
  req: Request,
  deps: GenerateHandlerDeps = {},
): Promise<Response> {
  if (req.method !== "POST") {
    return json({ error: "Method not allowed. Use POST." }, 405);
  }

  try {
    const body = await readJsonObject(req);
    if (!body.ok) {
      return json({ error: body.error }, 400);
    }

    const reqGate = validateGenerateRequest(body.value);
    if (!reqGate.ok) {
      return json({ error: reqGate.error }, 400);
    }
    const { prompt, patchId } = reqGate;

    // The brief is client-supplied (from /api/brief) — it feeds the
    // generator prompt, so sanitize + cap it like any other text.
    let briefArg = "";
    if (reqGate.brief !== undefined) {
      briefArg = truncate(sanitizePrompt(reqGate.brief), MAX_BRIEF_LENGTH);
    }

    // Rate limit BEFORE any upstream work (and before the key check, so
    // a missing-key deploy never hands out unlimited free probes).
    const gate = deps.gateRateLimit ?? gateRateLimit;
    const limited = await gate(req, "generate");
    if (limited !== null) return limited;

    const apiKey = (deps.getApiKey ?? (() => process.env.GEMINI_KEY))();
    if (typeof apiKey !== "string" || apiKey.length === 0) {
      return json({ error: "Generation is not configured (missing GEMINI_KEY)." }, 503);
    }

    // Global quota: count this attempt before Gemini (billable-start).
    const quota = deps.gateQuota ?? gateQuota;
    const capacity = await quota("generate");
    if (capacity !== null) return capacity;

    // C++ parity: the generator receives the sonic brief when one exists,
    // otherwise the sanitized raw prompt.
    const sanitized = sanitizePrompt(prompt);
    const promptForGenerator = briefArg.length > 0 ? briefArg : sanitized;

    const generate = deps.generatePatchText ?? generatePatchText;
    const note = deps.noteOutcome ?? noteOutcome;
    const generated = await generate(promptForGenerator, patchId, apiKey);
    if (!generated.ok) {
      // Upstream / safety failures count toward error-rate monitoring.
      // Validation 4xx above never reach here.
      await note(false);
      if (generated.reason === "blocked") return json({ error: SAFETY_400 }, 400);
      return json({ error: UPSTREAM_502 }, 502);
    }

    const assembled = assembleGeneratePayload(generated.text, promptForGenerator);
    if (!assembled.ok) {
      await note(false);
      return json({ error: assembled.error }, assembled.status);
    }
    await note(true);
    return json(assembled.payload, 200);
  } catch {
    return json({ error: "Internal server error." }, 500);
  }
}

export default async function handler(req: Request): Promise<Response> {
  return handleGenerate(req);
}
