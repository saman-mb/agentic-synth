// ── In-memory rate limiter for the Netlify function (#280) ──────────
//
// Free-tier safeguard: 3 generations per IP per minute (fixed minute
// window) and 200 generations per IP per UTC day. State is per-isolate
// in-memory — COLD-START SEMANTICS: every Netlify cold start (deploy,
// scale-out, idle eviction) resets all counters, so these limits are a
// soft guardrail against abuse bursts, NOT an exact quota. Deterministic
// testing injects the clock; the only Date.now() read is the `now`
// parameter default.

export const MINUTE_LIMIT = 3;
export const DAY_LIMIT = 200;
export const MAX_TRACKED_IPS = 10_000;

const MINUTE_MS = 60_000;

export interface IpWindow {
  // Fixed minute window: floor(now / 60_000) bucket + count.
  minuteBucket: number;
  minuteCount: number;
  // UTC day bucket: "YYYY-MM-DD" + count.
  dayKey: string;
  dayCount: number;
}

export interface RateLimitStore {
  /** Insertion-ordered so eviction can drop the oldest IP (Map order). */
  ips: Map<string, IpWindow>;
}

export function createRateLimitStore(): RateLimitStore {
  return { ips: new Map() };
}

function utcDayKey(now: number): string {
  return new Date(now).toISOString().slice(0, 10);
}

export interface RateLimitResult {
  allowed: boolean;
  /** Set when denied: which window tripped. */
  window?: "minute" | "day";
  minuteRemaining: number;
  dayRemaining: number;
}

/**
 * Check (and record) one generation attempt for `ip`. `now` is injected
 * so tests never sleep and never depend on wall-clock time.
 *
 * Lazy eviction: stale minute/day counters reset in place on touch;
 * once MAX_TRACKED_IPS distinct IPs are tracked, the oldest entry is
 * evicted to make room (bounded memory per isolate).
 */
export function checkRateLimit(
  store: RateLimitStore,
  ip: string,
  now: number = Date.now(),
): RateLimitResult {
  const minuteBucket = Math.floor(now / MINUTE_MS);
  const dayKey = utcDayKey(now);

  let entry = store.ips.get(ip);
  if (entry === undefined) {
    if (store.ips.size >= MAX_TRACKED_IPS) {
      // Evict oldest (Map preserves insertion order).
      const oldest = store.ips.keys().next();
      if (!oldest.done) store.ips.delete(oldest.value);
    }
    entry = { minuteBucket, minuteCount: 0, dayKey, dayCount: 0 };
    store.ips.set(ip, entry);
  }

  // Lazy reset of stale windows.
  if (entry.minuteBucket !== minuteBucket) {
    entry.minuteBucket = minuteBucket;
    entry.minuteCount = 0;
  }
  if (entry.dayKey !== dayKey) {
    entry.dayKey = dayKey;
    entry.dayCount = 0;
  }

  const minuteRemaining = Math.max(0, MINUTE_LIMIT - entry.minuteCount);
  const dayRemaining = Math.max(0, DAY_LIMIT - entry.dayCount);

  if (entry.dayCount >= DAY_LIMIT) {
    return { allowed: false, window: "day", minuteRemaining, dayRemaining: 0 };
  }
  if (entry.minuteCount >= MINUTE_LIMIT) {
    return { allowed: false, window: "minute", minuteRemaining: 0, dayRemaining };
  }

  entry.minuteCount += 1;
  entry.dayCount += 1;
  return {
    allowed: true,
    minuteRemaining: Math.max(0, MINUTE_LIMIT - entry.minuteCount),
    dayRemaining: Math.max(0, DAY_LIMIT - entry.dayCount),
  };
}
