// ── Global Gemini daily quota configuration (#310) ──────────────────
//
// Env-driven call + estimated-cost caps. Complements per-identity rate
// limits (#309). FAIL_MODE defaults closed — store errors never open
// unlimited upstream spend. Cost is estimated (no Gemini billing API);
// tune GEMINI_EST_USD_PER_* to match your model/pricing.

import type { FailMode, StoreMode } from "./rateLimitConfig.mts";

export interface QuotaConfig {
  /** Max Gemini attempts (billable starts) per UTC day. */
  dailyCallCap: number;
  /** Max estimated spend USD per UTC day. */
  dailyCostCapUsd: number;
  /** Estimated USD charged when /api/brief calls Gemini. */
  estUsdPerBrief: number;
  /** Estimated USD charged when /api/generate calls Gemini. */
  estUsdPerGenerate: number;
  /** Fraction of cost cap that triggers a pre-trip alert (default 0.8). */
  spendWarnFraction: number;
  failMode: FailMode;
  store: StoreMode;
}

/** Generous defaults for local/demo; tighten in Netlify env for prod. */
export const DEFAULT_DAILY_CALL_CAP = 10_000;
export const DEFAULT_DAILY_COST_CAP_USD = 10;
export const DEFAULT_EST_USD_PER_BRIEF = 0.0004;
export const DEFAULT_EST_USD_PER_GENERATE = 0.0008;
export const DEFAULT_SPEND_WARN_FRACTION = 0.8;

function parsePositiveInt(raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw.trim().length === 0) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 1) return fallback;
  return n;
}

function parsePositiveFloat(raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw.trim().length === 0) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || n <= 0) return fallback;
  return n;
}

function parseFraction(raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw.trim().length === 0) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || n <= 0 || n >= 1) return fallback;
  return n;
}

function parseFailMode(raw: string | undefined): FailMode {
  if (raw === undefined) return "closed";
  const v = raw.trim().toLowerCase();
  if (v === "open") return "open";
  return "closed";
}

function parseStoreMode(raw: string | undefined): StoreMode {
  if (raw === undefined) return "blobs";
  const v = raw.trim().toLowerCase();
  if (v === "memory") return "memory";
  return "blobs";
}

/** Parse quota env. `env` defaults to `process.env` for production. */
export function loadQuotaConfig(
  env: Record<string, string | undefined> = process.env,
): QuotaConfig {
  // QUOTA_STORE falls back to RATE_LIMIT_STORE so one knob covers both.
  const storeRaw = env.QUOTA_STORE ?? env.RATE_LIMIT_STORE;
  const failRaw = env.QUOTA_FAIL_MODE ?? env.RATE_LIMIT_FAIL_MODE;
  return {
    dailyCallCap: parsePositiveInt(env.GEMINI_DAILY_CALL_CAP, DEFAULT_DAILY_CALL_CAP),
    dailyCostCapUsd: parsePositiveFloat(
      env.GEMINI_DAILY_COST_CAP_USD,
      DEFAULT_DAILY_COST_CAP_USD,
    ),
    estUsdPerBrief: parsePositiveFloat(
      env.GEMINI_EST_USD_PER_BRIEF,
      DEFAULT_EST_USD_PER_BRIEF,
    ),
    estUsdPerGenerate: parsePositiveFloat(
      env.GEMINI_EST_USD_PER_GENERATE,
      DEFAULT_EST_USD_PER_GENERATE,
    ),
    spendWarnFraction: parseFraction(
      env.GEMINI_SPEND_WARN_FRACTION,
      DEFAULT_SPEND_WARN_FRACTION,
    ),
    failMode: parseFailMode(failRaw),
    store: parseStoreMode(storeRaw),
  };
}

export type QuotaResource = "brief" | "generate";

export function estimateUsdForResource(
  cfg: QuotaConfig,
  resource: QuotaResource,
): number {
  return resource === "brief" ? cfg.estUsdPerBrief : cfg.estUsdPerGenerate;
}

/** Integer micro-USD (1e-6 USD) for durable counter storage. */
export function usdToMicros(usd: number): number {
  if (!Number.isFinite(usd) || usd <= 0) return 0;
  return Math.max(0, Math.round(usd * 1_000_000));
}

export function microsToUsd(micros: number): number {
  return micros / 1_000_000;
}
