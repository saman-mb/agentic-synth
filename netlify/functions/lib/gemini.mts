// ── Gemini client for the Netlify function (#280) ───────────────────
//
// Parity port of the C++ mapper path: PromptSanitizer.cpp (word
// substitutions) + PromptEnhancer.cpp (brief generation) + the Gemini
// patch-generation call in PromptHandler.
//
// Never logs or echoes the API key; upstream failures surface fixed
// strings, never URLs. Node 20+ globals only, no external deps.

import { sanitizePrompt } from "../../../libs/prompt/src/index.ts";
import { ENHANCER_PROMPT, SYSTEM_PROMPT } from "./prompts.generated.mts";

export { sanitizePrompt };

// ---------------------------------------------------------------------------
// Shared request plumbing
// ---------------------------------------------------------------------------

const GEMINI_BASE = "https://generativelanguage.googleapis.com/v1beta/models";

function authedUrl(model: string, apiKey: string): string {
  // Kept in one place so no error path can accidentally interpolate it.
  return `${GEMINI_BASE}/${model}:generateContent?key=${encodeURIComponent(apiKey)}`;
}

async function postJson(
  url: string,
  body: unknown,
  timeoutMs: number,
  fetchImpl: typeof fetch,
): Promise<{ status: number; json: unknown }> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetchImpl(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
      signal: controller.signal,
    });
    const json: unknown = await res.json().catch(() => null);
    return { status: res.status, json };
  } finally {
    clearTimeout(timer);
  }
}

// ---------------------------------------------------------------------------
// Enhancer — brief generation (C++ PromptEnhancer parity)
// ---------------------------------------------------------------------------

// Gemini retired the 2.5 models for newly issued API keys (#280: the demo
// key gets 404 NOT_FOUND on them). 3.5/3.6 flash are the current free-tier
// equivalents; env overrides keep older (still-served) keys usable.
const ENHANCER_MODEL = process.env.GEMINI_ENHANCER_MODEL || "gemini-3.5-flash-lite";
const ENHANCER_TIMEOUT_MS = 6_000;

/**
 * Returns the sonic brief for `sanitized`. C++ parity: any failure
 * (timeout, non-200, empty/missing text) degrades gracefully — the
 * sanitized raw prompt becomes the brief. Never throws, never 500s.
 */
export async function enhanceBrief(
  sanitized: string,
  apiKey: string,
  fetchImpl: typeof fetch = fetch,
): Promise<string> {
  try {
    const body = {
      contents: [
        {
          parts: [
            {
              text:
                ENHANCER_PROMPT +
                "\n\nProducer prompt: " +
                sanitized +
                "\n\nEmit the brief now, starting at SONIC CHARACTER:",
            },
          ],
        },
      ],
      generationConfig: { temperature: 0.85 },
      // Deliberately NO responseMimeType — the brief is prose, not JSON.
    };
    const { status, json } = await postJson(
      authedUrl(ENHANCER_MODEL, apiKey),
      body,
      ENHANCER_TIMEOUT_MS,
      fetchImpl,
    );
    if (status !== 200) return sanitized;
    const text = extractCandidateText(json);
    return text !== null && text.trim().length > 0 ? text : sanitized;
  } catch {
    return sanitized;
  }
}

// ---------------------------------------------------------------------------
// Generator — patch JSON via Gemini flash-lite, 2 attempts
// ---------------------------------------------------------------------------

// Generator default is flash-lite: measured live, 3.6-flash thinking runs
// 10.6s — over the platform's 10s total execution cap on the free plan —
// while flash-lite produces equivalent patch JSON in ~2.8s (#280 r9).
const GENERATOR_MODEL = process.env.GEMINI_GENERATOR_MODEL || "gemini-3.5-flash-lite";
const PER_ATTEMPT_TIMEOUT_MS = 4_000;
// The platform kills a function at 10 s TOTAL on the free plan —
// streaming does not extend it (#280 r9 live finding). Each endpoint
// therefore runs ONE model call with a slim retry inside a 9 s wall.
const ABSOLUTE_DEADLINE_MS = 9_000;
const BACKOFF_MS = [300];

// Transient upstream error.status values worth one more attempt.
const RETRYABLE_STATUSES = new Set(["UNAVAILABLE", "INTERNAL", "DEADLINE_EXCEEDED"]);

export type GenerateResult =
  | { ok: true; text: string }
  | { ok: false; reason: "blocked"; message: string }
  | { ok: false; reason: "upstream"; message: string };

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

function asString(v: unknown): string | null {
  return typeof v === "string" && v.length > 0 ? v : null;
}

/** candidates[0].content.parts[*].text joined — the only path we read. */
function extractCandidateText(json: unknown): string | null {
  if (!isRecord(json)) return null;
  const candidates = json["candidates"];
  if (!Array.isArray(candidates) || candidates.length === 0) return null;
  const first = candidates[0];
  if (!isRecord(first)) return null;
  const content = first["content"];
  if (!isRecord(content)) return null;
  const parts = content["parts"];
  if (!Array.isArray(parts)) return null;
  const texts: string[] = [];
  for (const part of parts) {
    if (isRecord(part) && typeof part["text"] === "string") {
      texts.push(part["text"] as string);
    }
  }
  return texts.length > 0 ? texts.join("") : null;
}

/**
 * Strip markdown code fences (C++ parity): cut to the first newline
 * after a leading ``` and cut at the last ``` if one remains.
 */
function stripFences(text: string): string {
  let out = text;
  const lead = out.indexOf("```");
  if (lead !== -1 && out.slice(0, lead).trim() === "") {
    const nl = out.indexOf("\n", lead);
    out = nl === -1 ? "" : out.slice(nl + 1);
  }
  const tail = out.lastIndexOf("```");
  if (tail !== -1) out = out.slice(0, tail);
  return out.trim();
}

/** Fence-stripped model text ready for JSON.parse (exported for tests). */
export function prepareJsonText(text: string): string {
  return stripFences(text);
}

interface AttemptOutcome {
  kind: "retry" | "stop";
  // For "stop": final classification (undefined when text present).
  result?: GenerateResult;
  // For a successful attempt: the fence-stripped model text.
  text?: string;
}

/** Classify one HTTP response from the generator endpoint. */
function classify(status: number, json: unknown): AttemptOutcome {
  if (status !== 200) {
    if (status >= 500) return { kind: "retry" };
    // Non-5xx upstream rejection (auth, quota, bad request): never retry.
    return {
      kind: "stop",
      result: { ok: false, reason: "upstream", message: "Upstream model request failed." },
    };
  }

  if (!isRecord(json)) return { kind: "retry" };

  // Prompt-level block: promptFeedback.blockReason. Never retry.
  const feedback = json["promptFeedback"];
  if (isRecord(feedback) && asString(feedback["blockReason"]) !== null) {
    return {
      kind: "stop",
      result: {
        ok: false,
        reason: "blocked",
        message: "This prompt was blocked by safety filters. Try rewording it.",
      },
    };
  }

  // Body-level error object on a 200 (rare, but seen in the wild).
  const err = json["error"];
  if (isRecord(err)) {
    const errStatus = asString(err["status"]);
    if (errStatus !== null && RETRYABLE_STATUSES.has(errStatus)) return { kind: "retry" };
    return {
      kind: "stop",
      result: { ok: false, reason: "upstream", message: "Upstream model request failed." },
    };
  }

  const candidates = json["candidates"];
  const first = Array.isArray(candidates) && candidates.length > 0 ? candidates[0] : undefined;
  const finishReason = isRecord(first) ? asString(first["finishReason"]) : null;

  if (finishReason === "SAFETY") {
    return {
      kind: "stop",
      result: {
        ok: false,
        reason: "blocked",
        message: "This prompt was blocked by safety filters. Try rewording it.",
      },
    };
  }

  // Truncated output: worth one more attempt (never more than the
  // 3-attempt / 55 s budget the caller enforces).
  if (finishReason === "MAX_TOKENS") return { kind: "retry" };

  const text = extractCandidateText(json);
  if (text !== null && text.trim().length > 0) {
    if (finishReason !== null && finishReason !== "STOP") {
      // e.g. RECITATION — non-STOP output we cannot trust. No retry.
      return {
        kind: "stop",
        result: { ok: false, reason: "upstream", message: "Upstream model request failed." },
      };
    }
    return { kind: "stop", text: stripFences(text) };
  }

  // Empty / missing text.
  return { kind: "retry" };
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Patch generation with retry: 3 attempts, 200/600 ms backoff, 12 s
 * per-attempt timeout, 55 s absolute deadline (retries are skipped once
 * past it). Retries on network errors, empty responses, transient 5xx /
 * UNAVAILABLE / INTERNAL / DEADLINE_EXCEEDED, and MAX_TOKENS. Never
 * retries safety blocks or non-5xx failures.
 */
export async function generatePatchText(
  prompt: string,
  patchId: number,
  apiKey: string,
  fetchImpl: typeof fetch = fetch,
): Promise<GenerateResult> {
  const text =
    SYSTEM_PROMPT +
    "\n\nGenerate a JSON patch (patch_id=" +
    patchId +
    ") for: " +
    prompt +
    "\n\nReturn JSON only. No markdown. No prose. Match the schema exactly.";

  // Gemini 3.x rejects thinkingBudget: 0 with INVALID_ARGUMENT (thinking
  // is always on for those models); the zero-budget disable is a 2.5-era
  // parameter. Send it only for 2.5-family models (#280 live-key finding).
  const generationConfig: Record<string, unknown> = {
    temperature: 0.35,
    maxOutputTokens: 4096,
    responseMimeType: "application/json",
  };
  if (GENERATOR_MODEL.startsWith("gemini-2.5")) {
    generationConfig.thinkingConfig = { thinkingBudget: 0 };
  }

  const body = {
    contents: [{ parts: [{ text }] }],
    generationConfig,
    safetySettings: ["HARASSMENT", "HATE_SPEECH", "SEXUALLY_EXPLICIT", "DANGEROUS_CONTENT"].map(
      (category) => ({ category: `HARM_CATEGORY_${category}`, threshold: "BLOCK_ONLY_HIGH" }),
    ),
  };

  const startedAt = Date.now();
  const url = authedUrl(GENERATOR_MODEL, apiKey);

  for (let attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      const backoff = BACKOFF_MS[attempt - 1];
      if (Date.now() - startedAt + backoff >= ABSOLUTE_DEADLINE_MS) break;
      await sleep(backoff);
    }

    const remaining = ABSOLUTE_DEADLINE_MS - (Date.now() - startedAt);
    const timeout = Math.min(PER_ATTEMPT_TIMEOUT_MS, Math.max(remaining, 1));

    let response: { status: number; json: unknown };
    try {
      response = await postJson(url, body, timeout, fetchImpl);
    } catch {
      // Network error / abort → retryable.
      continue;
    }

    const outcome = classify(response.status, response.json);
    if (outcome.kind === "stop") {
      if (outcome.text !== undefined) return { ok: true, text: outcome.text };
      return outcome.result ?? {
        ok: false,
        reason: "upstream",
        message: "Upstream model request failed.",
      };
    }
    // Retryable: fall through to the next attempt.
  }

  return { ok: false, reason: "upstream", message: "Upstream model request failed." };
}
