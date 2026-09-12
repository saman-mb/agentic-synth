// ── Tiered rate-limit policy (#309) ─────────────────────────────────
//
// Pure checkAndConsume + durable enforceRateLimit. Clock is injected so
// tests never sleep. Store failures honour FAIL_MODE (default closed).

import type { Identity } from "./identity.mts";
import type { FailMode, TierLimits } from "./rateLimitConfig.mts";
import {
  storeKey,
  type RateLimitResource,
  type RateLimitStore,
  type WindowType,
} from "./rateLimitStore.mts";

const MINUTE_MS = 60_000;
const CAS_ATTEMPTS = 12;

export type RateLimitWindow = WindowType;

export interface CheckAndConsumeInput {
  minuteCount: number;
  dayCount: number;
  limits: TierLimits;
  now: number;
}

export interface CheckAndConsumeResult {
  allowed: boolean;
  /** Which window tripped when denied. */
  window?: RateLimitWindow;
  retryAfterSeconds?: number;
  /** Counts to persist when allowed (already include this request). */
  nextMinuteCount: number;
  nextDayCount: number;
  minuteWindowId: string;
  dayWindowId: string;
}

export function minuteWindowId(now: number): string {
  return String(Math.floor(now / MINUTE_MS));
}

export function dayWindowId(now: number): string {
  return new Date(now).toISOString().slice(0, 10);
}

export function retryAfterSeconds(window: RateLimitWindow, now: number): number {
  if (window === "minute") {
    const bucketEnd = (Math.floor(now / MINUTE_MS) + 1) * MINUTE_MS;
    return Math.max(1, Math.ceil((bucketEnd - now) / 1000));
  }
  // UTC midnight of the next day.
  const dayStart = Date.UTC(
    new Date(now).getUTCFullYear(),
    new Date(now).getUTCMonth(),
    new Date(now).getUTCDate(),
  );
  const nextDay = dayStart + 86_400_000;
  return Math.max(1, Math.ceil((nextDay - now) / 1000));
}

/**
 * Pure policy: given current window counts (before this request), decide
 * allow/deny and the next counts to write. Does not touch I/O.
 */
export function checkAndConsume(input: CheckAndConsumeInput): CheckAndConsumeResult {
  const { minuteCount, dayCount, limits, now } = input;
  const minuteId = minuteWindowId(now);
  const dayId = dayWindowId(now);

  if (dayCount >= limits.day) {
    return {
      allowed: false,
      window: "day",
      retryAfterSeconds: retryAfterSeconds("day", now),
      nextMinuteCount: minuteCount,
      nextDayCount: dayCount,
      minuteWindowId: minuteId,
      dayWindowId: dayId,
    };
  }
  if (minuteCount >= limits.minute) {
    return {
      allowed: false,
      window: "minute",
      retryAfterSeconds: retryAfterSeconds("minute", now),
      nextMinuteCount: minuteCount,
      nextDayCount: dayCount,
      minuteWindowId: minuteId,
      dayWindowId: dayId,
    };
  }

  return {
    allowed: true,
    nextMinuteCount: minuteCount + 1,
    nextDayCount: dayCount + 1,
    minuteWindowId: minuteId,
    dayWindowId: dayId,
  };
}

export function rateLimitedMessage(window: RateLimitWindow, tier: string): string {
  if (window === "day") {
    return tier === "paid"
      ? "You have reached today's generation limit. Please come back tomorrow (UTC)."
      : "You have reached the daily generation limit for the demo. Please come back tomorrow (UTC).";
  }
  return tier === "paid"
    ? "Too many generations in a short window. Please wait a minute and try again."
    : "The demo is busy right now. Please wait a minute and try again.";
}

export type EnforceRateLimitResult =
  | { ok: true }
  | {
      ok: false;
      kind: "rate_limited";
      window: RateLimitWindow;
      tier: string;
      retryAfterSeconds: number;
      error: string;
    }
  | { ok: false; kind: "unavailable"; error: string };

export interface EnforceRateLimitDeps {
  store: RateLimitStore;
  identity: Identity;
  resource: RateLimitResource;
  limits: TierLimits;
  failMode: FailMode;
  now?: number;
}

/**
 * Read both windows, apply pure policy, CAS-write increments when allowed.
 * Retries on CAS conflict. Store errors → unavailable (caller maps to 503
 * when failMode is closed) or allow when failMode is open.
 */
export async function enforceRateLimit(
  deps: EnforceRateLimitDeps,
): Promise<EnforceRateLimitResult> {
  const now = deps.now ?? Date.now();
  const { store, identity, resource, limits, failMode } = deps;

  try {
    for (let attempt = 0; attempt < CAS_ATTEMPTS; attempt += 1) {
      const minuteId = minuteWindowId(now);
      const dayId = dayWindowId(now);
      const minuteKey = storeKey(identity.tier, identity.subject, resource, "minute", minuteId);
      const dayKey = storeKey(identity.tier, identity.subject, resource, "day", dayId);

      const [minuteSnap, daySnap] = await Promise.all([
        store.get(minuteKey),
        store.get(dayKey),
      ]);

      const decision = checkAndConsume({
        minuteCount: minuteSnap.count,
        dayCount: daySnap.count,
        limits,
        now,
      });

      if (!decision.allowed) {
        const window = decision.window ?? "minute";
        const retry = decision.retryAfterSeconds ?? retryAfterSeconds(window, now);
        return {
          ok: false,
          kind: "rate_limited",
          window,
          tier: identity.tier,
          retryAfterSeconds: retry,
          error: rateLimitedMessage(window, identity.tier),
        };
      }

      const minuteWrite = await store.compareAndSet(
        minuteKey,
        { count: decision.nextMinuteCount },
        minuteSnap.etag,
      );
      if (!minuteWrite.ok) continue;

      const dayWrite = await store.compareAndSet(
        dayKey,
        { count: decision.nextDayCount },
        daySnap.etag,
      );
      if (!dayWrite.ok) {
        // Minute slot consumed; day CAS lost a race. Retry from read —
        // worst case a minute count ticks without a paired day tick once.
        continue;
      }

      return { ok: true };
    }

    // Exhausted CAS retries — treat as store contention / unavailable.
    if (failMode === "open") return { ok: true };
    return {
      ok: false,
      kind: "unavailable",
      error: "Rate limiting is temporarily unavailable. Please try again shortly.",
    };
  } catch {
    if (failMode === "open") return { ok: true };
    return {
      ok: false,
      kind: "unavailable",
      error: "Rate limiting is temporarily unavailable. Please try again shortly.",
    };
  }
}

/** JSON body helpers for HTTP responses. */
export function rateLimitedBody(
  result: Extract<EnforceRateLimitResult, { kind: "rate_limited" }>,
): Record<string, unknown> {
  return {
    error: result.error,
    code: "rate_limited",
    window: result.window,
    tier: result.tier,
    retry_after_seconds: result.retryAfterSeconds,
  };
}

export function rateLimitUnavailableBody(
  result: Extract<EnforceRateLimitResult, { kind: "unavailable" }>,
): Record<string, unknown> {
  return {
    error: result.error,
    code: "rate_limit_unavailable",
  };
}
