// ── Operator alerts (#313) ──────────────────────────────────────────
//
// Fire-and-forget alerts for spend warn, hard quota trip, elevated
// error rate, and abuse (identity hammering 429s). No dashboard —
// webhook and/or structured console logs with a runbook path.
// Dedup claims use the same CAS counter store as rate limits / quota.

import type { RateLimitStore } from "./rateLimitStore.mts";

export type AlertKind =
  | "spend_warn"
  | "quota_hard_cap"
  | "error_rate"
  | "abuse_429";

export interface AlertPayload {
  kind: AlertKind;
  severity: "warn" | "critical";
  message: string;
  /** Repo-relative runbook path operators should open. */
  runbook: string;
  details: Record<string, unknown>;
  at: string;
}

export interface AlertSink {
  emit(payload: AlertPayload): Promise<void>;
}

export const RUNBOOK_GEMINI_SPEND = "docs/runbooks/gemini-spend.md";
export const RUNBOOK_ABUSE_429 = "docs/runbooks/abuse-429.md";

/** Structured console sink — always available on Netlify. */
export class ConsoleAlertSink implements AlertSink {
  async emit(payload: AlertPayload): Promise<void> {
    // Single JSON line so log drains can filter on "agsynth_alert".
    console.error(
      JSON.stringify({
        agsynth_alert: true,
        ...payload,
      }),
    );
  }
}

/** Optional webhook (Slack/Discord/PagerDuty-compatible JSON POST). */
export class WebhookAlertSink implements AlertSink {
  private readonly url: string;
  private readonly fetchImpl: typeof fetch;

  constructor(url: string, fetchImpl: typeof fetch = fetch) {
    this.url = url;
    this.fetchImpl = fetchImpl;
  }

  async emit(payload: AlertPayload): Promise<void> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 3_000);
    try {
      await this.fetchImpl(this.url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
        signal: controller.signal,
      });
    } finally {
      clearTimeout(timer);
    }
  }
}

export class MultiAlertSink implements AlertSink {
  private readonly sinks: readonly AlertSink[];

  constructor(sinks: readonly AlertSink[]) {
    this.sinks = sinks;
  }

  async emit(payload: AlertPayload): Promise<void> {
    await Promise.allSettled(this.sinks.map((s) => s.emit(payload)));
  }
}

export function buildAlertSink(
  env: Record<string, string | undefined> = process.env,
  fetchImpl: typeof fetch = fetch,
): AlertSink {
  const sinks: AlertSink[] = [new ConsoleAlertSink()];
  const url = env.ALERT_WEBHOOK_URL?.trim();
  if (url !== undefined && url.length > 0) {
    sinks.push(new WebhookAlertSink(url, fetchImpl));
  }
  return new MultiAlertSink(sinks);
}

/**
 * CAS-claim a one-shot alert key (absent → 1). Returns true when this
 * caller won the claim and should emit. Failures → skip emit (avoid
 * alert storms when the store is down; quota itself fails closed).
 */
export async function claimAlertSlot(
  store: RateLimitStore,
  key: string,
): Promise<boolean> {
  try {
    for (let i = 0; i < 8; i += 1) {
      const snap = await store.get(key);
      if (snap.count >= 1) return false;
      const write = await store.compareAndSet(key, { count: 1 }, snap.etag);
      if (write.ok) return true;
    }
    return false;
  } catch {
    return false;
  }
}

export interface FireAlertDeps {
  sink: AlertSink;
  store?: RateLimitStore;
  /** When set, emit at most once per claim key. */
  dedupeKey?: string;
  now?: number;
}

/**
 * Emit an alert (optionally deduped). Never throws to callers — alert
 * path must not break request handling.
 */
export async function fireAlert(
  partial: Omit<AlertPayload, "at">,
  deps: FireAlertDeps,
): Promise<boolean> {
  const now = deps.now ?? Date.now();
  try {
    if (deps.dedupeKey !== undefined && deps.store !== undefined) {
      const claimed = await claimAlertSlot(deps.store, deps.dedupeKey);
      if (!claimed) return false;
    }
    const payload: AlertPayload = {
      ...partial,
      at: new Date(now).toISOString(),
    };
    await deps.sink.emit(payload);
    return true;
  } catch {
    return false;
  }
}

export function alertDedupeKey(kind: AlertKind, windowId: string, subject?: string): string {
  if (subject !== undefined) {
    return `alert/v1/${kind}/${windowId}/${subject}`;
  }
  return `alert/v1/${kind}/${windowId}`;
}
