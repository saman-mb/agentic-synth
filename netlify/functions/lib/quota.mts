// ── Global Gemini daily quota (#310) ────────────────────────────────
//
// Sibling of per-identity rateLimit.mts — global UTC-day call + estimated
// cost counters. Check + consume BEFORE the Gemini HTTP call so attempts
// that would bill count even when upstream fails. Store failures honour
// FAIL_MODE (default closed → 503 capacity).

import {
  alertDedupeKey,
  fireAlert,
  RUNBOOK_GEMINI_SPEND,
  type AlertSink,
} from "./alerts.mts";
import {
  estimateUsdForResource,
  microsToUsd,
  usdToMicros,
  type QuotaConfig,
  type QuotaResource,
} from "./quotaConfig.mts";
import { dayWindowId, retryAfterSeconds } from "./rateLimit.mts";
import type { FailMode } from "./rateLimitConfig.mts";
import type { RateLimitStore } from "./rateLimitStore.mts";

const CAS_ATTEMPTS = 12;

export function quotaCallsKey(dayId: string): string {
  return `quota/v1/gemini/${dayId}/calls`;
}

export function quotaCostMicrosKey(dayId: string): string {
  return `quota/v1/gemini/${dayId}/cost_micros`;
}

export interface CheckAndConsumeQuotaInput {
  calls: number;
  costMicros: number;
  addCostMicros: number;
  callCap: number;
  costCapMicros: number;
  spendWarnFraction: number;
  now: number;
}

export type QuotaDenyReason = "calls" | "cost";

export interface CheckAndConsumeQuotaResult {
  allowed: boolean;
  reason?: QuotaDenyReason;
  retryAfterSeconds?: number;
  nextCalls: number;
  nextCostMicros: number;
  dayWindowId: string;
  /** True when this consume crosses the soft spend warn threshold. */
  crossedSpendWarn: boolean;
  calls: number;
  costMicros: number;
}

/**
 * Pure policy: deny if either cap already reached; else reserve one call
 * and the estimated cost. Does not touch I/O.
 */
export function checkAndConsumeQuota(
  input: CheckAndConsumeQuotaInput,
): CheckAndConsumeQuotaResult {
  const {
    calls,
    costMicros,
    addCostMicros,
    callCap,
    costCapMicros,
    spendWarnFraction,
    now,
  } = input;
  const dayId = dayWindowId(now);
  const warnThreshold = Math.floor(costCapMicros * spendWarnFraction);

  if (calls >= callCap) {
    return {
      allowed: false,
      reason: "calls",
      retryAfterSeconds: retryAfterSeconds("day", now),
      nextCalls: calls,
      nextCostMicros: costMicros,
      dayWindowId: dayId,
      crossedSpendWarn: false,
      calls,
      costMicros,
    };
  }
  if (costMicros >= costCapMicros) {
    return {
      allowed: false,
      reason: "cost",
      retryAfterSeconds: retryAfterSeconds("day", now),
      nextCalls: calls,
      nextCostMicros: costMicros,
      dayWindowId: dayId,
      crossedSpendWarn: false,
      calls,
      costMicros,
    };
  }

  const nextCalls = calls + 1;
  const nextCostMicros = costMicros + addCostMicros;
  const crossedSpendWarn =
    costMicros < warnThreshold && nextCostMicros >= warnThreshold;

  return {
    allowed: true,
    nextCalls,
    nextCostMicros,
    dayWindowId: dayId,
    crossedSpendWarn,
    calls,
    costMicros,
  };
}

export type EnforceQuotaResult =
  | {
      ok: true;
      dayWindowId: string;
      calls: number;
      costMicros: number;
      crossedSpendWarn: boolean;
    }
  | {
      ok: false;
      kind: "capacity";
      reason: QuotaDenyReason;
      retryAfterSeconds: number;
      error: string;
      dayWindowId: string;
      calls: number;
      costMicros: number;
    }
  | { ok: false; kind: "unavailable"; error: string };

export interface EnforceQuotaDeps {
  store: RateLimitStore;
  config: QuotaConfig;
  resource: QuotaResource;
  failMode: FailMode;
  sink: AlertSink;
  now?: number;
}

const CAPACITY_MSG =
  "Daily generation capacity has been reached. Please try again tomorrow (UTC).";

/**
 * Read global day counters, apply pure policy, CAS-write when allowed.
 * Counts the attempt before Gemini (billable-start accounting).
 */
export async function enforceQuota(deps: EnforceQuotaDeps): Promise<EnforceQuotaResult> {
  const now = deps.now ?? Date.now();
  const { store, config, resource, failMode, sink } = deps;
  const addCostMicros = usdToMicros(estimateUsdForResource(config, resource));
  const costCapMicros = usdToMicros(config.dailyCostCapUsd);

  try {
    for (let attempt = 0; attempt < CAS_ATTEMPTS; attempt += 1) {
      const dayId = dayWindowId(now);
      const callsKey = quotaCallsKey(dayId);
      const costKey = quotaCostMicrosKey(dayId);

      const [callsSnap, costSnap] = await Promise.all([
        store.get(callsKey),
        store.get(costKey),
      ]);

      const decision = checkAndConsumeQuota({
        calls: callsSnap.count,
        costMicros: costSnap.count,
        addCostMicros,
        callCap: config.dailyCallCap,
        costCapMicros,
        spendWarnFraction: config.spendWarnFraction,
        now,
      });

      if (!decision.allowed) {
        const reason = decision.reason ?? "calls";
        const retry = decision.retryAfterSeconds ?? retryAfterSeconds("day", now);
        // Cap trip must never be silent (#310 / #313).
        await fireAlert(
          {
            kind: "quota_hard_cap",
            severity: "critical",
            message: `Gemini daily ${reason} cap reached (${decision.calls} calls, $${microsToUsd(decision.costMicros).toFixed(4)} est).`,
            runbook: RUNBOOK_GEMINI_SPEND,
            details: {
              reason,
              calls: decision.calls,
              call_cap: config.dailyCallCap,
              cost_usd: microsToUsd(decision.costMicros),
              cost_cap_usd: config.dailyCostCapUsd,
              day: dayId,
            },
          },
          {
            sink,
            store,
            dedupeKey: alertDedupeKey("quota_hard_cap", dayId),
            now,
          },
        );
        return {
          ok: false,
          kind: "capacity",
          reason,
          retryAfterSeconds: retry,
          error: CAPACITY_MSG,
          dayWindowId: dayId,
          calls: decision.calls,
          costMicros: decision.costMicros,
        };
      }

      const callsWrite = await store.compareAndSet(
        callsKey,
        { count: decision.nextCalls },
        callsSnap.etag,
      );
      if (!callsWrite.ok) continue;

      const costWrite = await store.compareAndSet(
        costKey,
        { count: decision.nextCostMicros },
        costSnap.etag,
      );
      if (!costWrite.ok) continue;

      if (decision.crossedSpendWarn) {
        await fireAlert(
          {
            kind: "spend_warn",
            severity: "warn",
            message: `Gemini daily estimated spend crossed ${Math.round(config.spendWarnFraction * 100)}% of cap.`,
            runbook: RUNBOOK_GEMINI_SPEND,
            details: {
              calls: decision.nextCalls,
              call_cap: config.dailyCallCap,
              cost_usd: microsToUsd(decision.nextCostMicros),
              cost_cap_usd: config.dailyCostCapUsd,
              warn_fraction: config.spendWarnFraction,
              day: dayId,
            },
          },
          {
            sink,
            store,
            dedupeKey: alertDedupeKey("spend_warn", dayId),
            now,
          },
        );
      }

      return {
        ok: true,
        dayWindowId: dayId,
        calls: decision.nextCalls,
        costMicros: decision.nextCostMicros,
        crossedSpendWarn: decision.crossedSpendWarn,
      };
    }

    if (failMode === "open") {
      return {
        ok: true,
        dayWindowId: dayWindowId(now),
        calls: 0,
        costMicros: 0,
        crossedSpendWarn: false,
      };
    }
    return {
      ok: false,
      kind: "unavailable",
      error: "Capacity tracking is temporarily unavailable. Please try again shortly.",
    };
  } catch {
    if (failMode === "open") {
      return {
        ok: true,
        dayWindowId: dayWindowId(now),
        calls: 0,
        costMicros: 0,
        crossedSpendWarn: false,
      };
    }
    return {
      ok: false,
      kind: "unavailable",
      error: "Capacity tracking is temporarily unavailable. Please try again shortly.",
    };
  }
}

export function capacityBody(
  result: Extract<EnforceQuotaResult, { kind: "capacity" }>,
): Record<string, unknown> {
  return {
    error: result.error,
    code: "capacity_exhausted",
    reason: result.reason,
    retry_after_seconds: result.retryAfterSeconds,
  };
}

export function quotaUnavailableBody(
  result: Extract<EnforceQuotaResult, { kind: "unavailable" }>,
): Record<string, unknown> {
  return {
    error: result.error,
    code: "capacity_unavailable",
  };
}
