// ── Lightweight ops metrics for alerts (#313) ───────────────────────
//
// Error-rate window + per-subject 429 hammering. Uses the same CAS
// counter store; in-memory works for tests / single process.

import {
  alertDedupeKey,
  fireAlert,
  RUNBOOK_ABUSE_429,
  RUNBOOK_GEMINI_SPEND,
  type AlertSink,
} from "./alerts.mts";
import { minuteWindowId } from "./rateLimit.mts";
import type { RateLimitStore } from "./rateLimitStore.mts";

const MINUTE_MS = 60_000;
const CAS_ATTEMPTS = 8;

export interface OpsAlertConfig {
  /** Error fraction over the window that trips an alert (default 0.05). */
  errorRateThreshold: number;
  /** Sliding window length in minutes (default 10). */
  errorRateWindowMinutes: number;
  /** Minimum (ok+err) samples before evaluating error rate. */
  errorRateMinSamples: number;
  /** 429s from one subject in one minute that trip abuse alert. */
  abuse429PerMinute: number;
}

export const DEFAULT_OPS_ALERT_CONFIG: OpsAlertConfig = {
  errorRateThreshold: 0.05,
  errorRateWindowMinutes: 10,
  errorRateMinSamples: 20,
  abuse429PerMinute: 30,
};

export function loadOpsAlertConfig(
  env: Record<string, string | undefined> = process.env,
): OpsAlertConfig {
  return {
    errorRateThreshold: parseUnitInterval(
      env.ALERT_ERROR_RATE_THRESHOLD,
      DEFAULT_OPS_ALERT_CONFIG.errorRateThreshold,
    ),
    errorRateWindowMinutes: parsePositiveInt(
      env.ALERT_ERROR_RATE_WINDOW_MINUTES,
      DEFAULT_OPS_ALERT_CONFIG.errorRateWindowMinutes,
    ),
    errorRateMinSamples: parsePositiveInt(
      env.ALERT_ERROR_RATE_MIN_SAMPLES,
      DEFAULT_OPS_ALERT_CONFIG.errorRateMinSamples,
    ),
    abuse429PerMinute: parsePositiveInt(
      env.ALERT_ABUSE_429_PER_MINUTE,
      DEFAULT_OPS_ALERT_CONFIG.abuse429PerMinute,
    ),
  };
}

function parsePositiveInt(raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw.trim().length === 0) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 1) return fallback;
  return n;
}

function parseUnitInterval(raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw.trim().length === 0) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || n <= 0 || n >= 1) return fallback;
  return n;
}

export function outcomeOkKey(minuteId: string): string {
  return `metrics/v1/outcome/${minuteId}/ok`;
}

export function outcomeErrKey(minuteId: string): string {
  return `metrics/v1/outcome/${minuteId}/err`;
}

export function abuse429Key(subject: string, minuteId: string): string {
  // Subject may contain colons; never slashes.
  return `metrics/v1/429/${subject}/${minuteId}`;
}

async function bumpCounter(store: RateLimitStore, key: string): Promise<number> {
  for (let i = 0; i < CAS_ATTEMPTS; i += 1) {
    const snap = await store.get(key);
    const next = snap.count + 1;
    const write = await store.compareAndSet(key, { count: next }, snap.etag);
    if (write.ok) return next;
  }
  return -1;
}

export interface RecordOutcomeDeps {
  store: RateLimitStore;
  sink: AlertSink;
  config: OpsAlertConfig;
  ok: boolean;
  now?: number;
}

/**
 * Record a post-gate request outcome and maybe fire an error-rate alert.
 * `ok=false` = upstream/5xx-class failure worth paging on (not 4xx validation).
 */
export async function recordOutcome(deps: RecordOutcomeDeps): Promise<void> {
  const now = deps.now ?? Date.now();
  const minuteId = minuteWindowId(now);
  const key = deps.ok ? outcomeOkKey(minuteId) : outcomeErrKey(minuteId);
  try {
    await bumpCounter(deps.store, key);
    await maybeAlertErrorRate(deps.store, deps.sink, deps.config, now);
  } catch {
    // Metrics must not break handlers.
  }
}

async function sumWindow(
  store: RateLimitStore,
  now: number,
  windowMinutes: number,
): Promise<{ ok: number; err: number }> {
  let ok = 0;
  let err = 0;
  const current = Math.floor(now / MINUTE_MS);
  for (let i = 0; i < windowMinutes; i += 1) {
    const id = String(current - i);
    const [okSnap, errSnap] = await Promise.all([
      store.get(outcomeOkKey(id)),
      store.get(outcomeErrKey(id)),
    ]);
    ok += okSnap.count;
    err += errSnap.count;
  }
  return { ok, err };
}

async function maybeAlertErrorRate(
  store: RateLimitStore,
  sink: AlertSink,
  config: OpsAlertConfig,
  now: number,
): Promise<void> {
  const { ok, err } = await sumWindow(store, now, config.errorRateWindowMinutes);
  const total = ok + err;
  if (total < config.errorRateMinSamples) return;
  const rate = err / total;
  if (rate < config.errorRateThreshold) return;

  const windowId = minuteWindowId(now);
  await fireAlert(
    {
      kind: "error_rate",
      severity: "critical",
      message: `Gemini path error rate ${(rate * 100).toFixed(1)}% over ${config.errorRateWindowMinutes}m (threshold ${config.errorRateThreshold * 100}%).`,
      runbook: RUNBOOK_GEMINI_SPEND,
      details: {
        ok,
        err,
        total,
        rate,
        threshold: config.errorRateThreshold,
        window_minutes: config.errorRateWindowMinutes,
      },
    },
    {
      sink,
      store,
      dedupeKey: alertDedupeKey("error_rate", windowId),
      now,
    },
  );
}

export interface RecordRateLimitedDeps {
  store: RateLimitStore;
  sink: AlertSink;
  config: OpsAlertConfig;
  subject: string;
  now?: number;
}

/** Count a 429 for a subject; alert when hammering threshold is crossed. */
export async function recordRateLimited(deps: RecordRateLimitedDeps): Promise<void> {
  const now = deps.now ?? Date.now();
  const minuteId = minuteWindowId(now);
  try {
    const count = await bumpCounter(deps.store, abuse429Key(deps.subject, minuteId));
    if (count < 0 || count < deps.config.abuse429PerMinute) return;

    await fireAlert(
      {
        kind: "abuse_429",
        severity: "warn",
        message: `Identity ${deps.subject} hit ${count} rate-limit 429s in one minute.`,
        runbook: RUNBOOK_ABUSE_429,
        details: {
          subject: deps.subject,
          count,
          threshold: deps.config.abuse429PerMinute,
          minute: minuteId,
        },
      },
      {
        sink: deps.sink,
        store: deps.store,
        dedupeKey: alertDedupeKey("abuse_429", minuteId, deps.subject),
        now,
      },
    );
  } catch {
    // ignore
  }
}
