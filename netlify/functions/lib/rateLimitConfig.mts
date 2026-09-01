// ── Rate-limit configuration (#309) ─────────────────────────────────
//
// Env-driven limits and fail/store modes for /api/brief and /api/generate.
// Defaults match the demo soft guardrail (3/min, 200/day) and a paid tier
// (30/min, 2000/day). FAIL_MODE defaults closed — store errors never open
// unlimited free probes. Real App Store JWT verify is #312; see entitlement.

export type RateLimitTier = "demo" | "paid";
export type FailMode = "closed" | "open";
export type StoreMode = "blobs" | "memory";

export interface TierLimits {
  minute: number;
  day: number;
}

export interface RateLimitConfig {
  demo: TierLimits;
  paid: TierLimits;
  failMode: FailMode;
  store: StoreMode;
  /** Explicit Bearer tokens treated as paid (non-prod / test only). */
  paidStubTokens: ReadonlySet<string>;
  /** When true, Bearer tokens with prefix `paidstub.` map to paid subjects. */
  allowPaidStubPrefix: boolean;
}

export const DEFAULT_DEMO_LIMITS: TierLimits = { minute: 3, day: 200 };
export const DEFAULT_PAID_LIMITS: TierLimits = { minute: 30, day: 2000 };

function parsePositiveInt(raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw.trim().length === 0) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 1) return fallback;
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

function parseTokenSet(raw: string | undefined): ReadonlySet<string> {
  if (raw === undefined || raw.trim().length === 0) return new Set();
  const tokens = raw
    .split(",")
    .map((t) => t.trim())
    .filter((t) => t.length > 0);
  return new Set(tokens);
}

/** Parse rate-limit env. `env` defaults to `process.env` for production. */
export function loadRateLimitConfig(
  env: Record<string, string | undefined> = process.env,
): RateLimitConfig {
  return {
    demo: {
      minute: parsePositiveInt(env.RATE_LIMIT_DEMO_MINUTE, DEFAULT_DEMO_LIMITS.minute),
      day: parsePositiveInt(env.RATE_LIMIT_DEMO_DAY, DEFAULT_DEMO_LIMITS.day),
    },
    paid: {
      minute: parsePositiveInt(env.RATE_LIMIT_PAID_MINUTE, DEFAULT_PAID_LIMITS.minute),
      day: parsePositiveInt(env.RATE_LIMIT_PAID_DAY, DEFAULT_PAID_LIMITS.day),
    },
    failMode: parseFailMode(env.RATE_LIMIT_FAIL_MODE),
    store: parseStoreMode(env.RATE_LIMIT_STORE),
    paidStubTokens: parseTokenSet(env.RATE_LIMIT_PAID_STUB_TOKENS),
    allowPaidStubPrefix: env.RATE_LIMIT_ALLOW_PAID_STUB === "1",
  };
}

export function limitsForTier(cfg: RateLimitConfig, tier: RateLimitTier): TierLimits {
  return tier === "paid" ? cfg.paid : cfg.demo;
}
