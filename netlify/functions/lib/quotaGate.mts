// ── Shared global quota + ops metrics gate (#310 / #313) ────────────
//
// Lazy singleton: quota config, ops alert config, alert sink, store.
// Handlers call gateQuota AFTER per-identity rate-limit allow and
// BEFORE Gemini. Outcomes / 429 hammering feed opsMetrics alerts.

import {
  buildAlertSink,
  type AlertSink,
} from "./alerts.mts";
import {
  capacityBody,
  enforceQuota,
  quotaUnavailableBody,
} from "./quota.mts";
import {
  loadQuotaConfig,
  type QuotaConfig,
  type QuotaResource,
} from "./quotaConfig.mts";
import {
  loadOpsAlertConfig,
  recordOutcome,
  recordRateLimited,
  type OpsAlertConfig,
} from "./opsMetrics.mts";
import {
  createBlobsRateLimitStore,
  MemoryStore,
  type RateLimitStore,
} from "./rateLimitStore.mts";

export interface QuotaRuntime {
  config: QuotaConfig;
  ops: OpsAlertConfig;
  sink: AlertSink;
  store: RateLimitStore;
}

let runtimePromise: Promise<QuotaRuntime> | undefined;

/** Test seam: replace or clear the lazy singleton. */
export function resetQuotaRuntime(): void {
  runtimePromise = undefined;
}

export function setQuotaRuntime(runtime: QuotaRuntime): void {
  runtimePromise = Promise.resolve(runtime);
}

async function buildRuntime(): Promise<QuotaRuntime> {
  const config = loadQuotaConfig();
  const ops = loadOpsAlertConfig();
  const sink = buildAlertSink();
  let store: RateLimitStore;
  if (config.store === "memory") {
    store = new MemoryStore();
  } else {
    try {
      // Same Blobs store name as rate limits; keys are quota/metrics prefixed.
      store = await createBlobsRateLimitStore();
    } catch {
      store = {
        async get() {
          throw new Error("quota blobs store unavailable");
        },
        async compareAndSet() {
          throw new Error("quota blobs store unavailable");
        },
      };
    }
  }
  return { config, ops, sink, store };
}

export function getQuotaRuntime(): Promise<QuotaRuntime> {
  if (runtimePromise === undefined) {
    runtimePromise = buildRuntime();
  }
  return runtimePromise;
}

function json(payload: unknown, status: number, extraHeaders?: HeadersInit): Response {
  const headers = new Headers(extraHeaders);
  headers.set("Content-Type", "application/json");
  return new Response(JSON.stringify(payload), { status, headers });
}

/**
 * Global daily Gemini quota. Returns a Response to short-circuit, or
 * null when the request may proceed to the upstream call.
 */
export async function gateQuota(
  resource: QuotaResource,
  now: number = Date.now(),
): Promise<Response | null> {
  const { config, sink, store } = await getQuotaRuntime();
  const result = await enforceQuota({
    store,
    config,
    resource,
    failMode: config.failMode,
    sink,
    now,
  });

  if (result.ok) return null;

  if (result.kind === "unavailable") {
    return json(quotaUnavailableBody(result), 503);
  }

  return json(capacityBody(result), 503, {
    "Retry-After": String(result.retryAfterSeconds),
  });
}

/** Record success/failure after a Gemini attempt (or clear failure). */
export async function noteOutcome(
  ok: boolean,
  now: number = Date.now(),
): Promise<void> {
  const { store, sink, ops } = await getQuotaRuntime();
  await recordOutcome({ store, sink, config: ops, ok, now });
}

/** Record a per-identity 429 for abuse detection. */
export async function noteRateLimited(
  subject: string,
  now: number = Date.now(),
): Promise<void> {
  const { store, sink, ops } = await getQuotaRuntime();
  await recordRateLimited({ store, sink, config: ops, subject, now });
}
