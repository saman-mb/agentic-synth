// ── Quota + alerts unit tests (#310 / #313) ─────────────────────────
//
// MemoryStore + injected clock. Covers call/cost caps, UTC day reset,
// spend-warn + hard-cap alerts, error-rate and abuse-429 alerts,
// fail-closed store errors.

import { describe, it, beforeEach } from "node:test";
import assert from "node:assert/strict";

import {
  alertDedupeKey,
  claimAlertSlot,
  fireAlert,
  RUNBOOK_ABUSE_429,
  RUNBOOK_GEMINI_SPEND,
  type AlertPayload,
  type AlertSink,
} from "./alerts.mts";
import {
  capacityBody,
  checkAndConsumeQuota,
  enforceQuota,
  quotaCallsKey,
  quotaCostMicrosKey,
} from "./quota.mts";
import {
  DEFAULT_DAILY_CALL_CAP,
  DEFAULT_DAILY_COST_CAP_USD,
  loadQuotaConfig,
  usdToMicros,
} from "./quotaConfig.mts";
import {
  abuse429Key,
  loadOpsAlertConfig,
  recordOutcome,
  recordRateLimited,
} from "./opsMetrics.mts";
import { dayWindowId, minuteWindowId } from "./rateLimit.mts";
import { MemoryStore, type RateLimitStore } from "./rateLimitStore.mts";

const FIXED_NOW = Date.UTC(2026, 8, 1, 12, 0, 30); // 2026-09-01T12:00:30.000Z
const NEXT_DAY = Date.UTC(2026, 8, 2, 0, 0, 1);

class CapturingSink implements AlertSink {
  readonly payloads: AlertPayload[] = [];
  async emit(payload: AlertPayload): Promise<void> {
    this.payloads.push(payload);
  }
}

function testQuotaConfig(overrides: Partial<ReturnType<typeof loadQuotaConfig>> = {}) {
  return {
    ...loadQuotaConfig({
      GEMINI_DAILY_CALL_CAP: "100",
      GEMINI_DAILY_COST_CAP_USD: "1",
      GEMINI_EST_USD_PER_BRIEF: "0.1",
      GEMINI_EST_USD_PER_GENERATE: "0.1",
      GEMINI_SPEND_WARN_FRACTION: "0.8",
      QUOTA_FAIL_MODE: "closed",
      QUOTA_STORE: "memory",
    }),
    ...overrides,
  };
}

describe("loadQuotaConfig", () => {
  it("applies defaults and fail-closed", () => {
    const cfg = loadQuotaConfig({});
    assert.equal(cfg.dailyCallCap, DEFAULT_DAILY_CALL_CAP);
    assert.equal(cfg.dailyCostCapUsd, DEFAULT_DAILY_COST_CAP_USD);
    assert.equal(cfg.failMode, "closed");
    assert.equal(cfg.store, "blobs");
    assert.equal(cfg.spendWarnFraction, 0.8);
  });

  it("parses env overrides and inherits RATE_LIMIT_STORE", () => {
    const cfg = loadQuotaConfig({
      GEMINI_DAILY_CALL_CAP: "50",
      GEMINI_DAILY_COST_CAP_USD: "2.5",
      RATE_LIMIT_STORE: "memory",
      RATE_LIMIT_FAIL_MODE: "open",
    });
    assert.equal(cfg.dailyCallCap, 50);
    assert.equal(cfg.dailyCostCapUsd, 2.5);
    assert.equal(cfg.store, "memory");
    assert.equal(cfg.failMode, "open");
  });
});

describe("checkAndConsumeQuota (pure)", () => {
  it("allows under caps and increments call + cost", () => {
    const r = checkAndConsumeQuota({
      calls: 0,
      costMicros: 0,
      addCostMicros: usdToMicros(0.1),
      callCap: 10,
      costCapMicros: usdToMicros(1),
      spendWarnFraction: 0.8,
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, true);
    assert.equal(r.nextCalls, 1);
    assert.equal(r.nextCostMicros, usdToMicros(0.1));
    assert.equal(r.dayWindowId, "2026-09-01");
    assert.equal(r.crossedSpendWarn, false);
  });

  it("denies on call cap with no Gemini-side increment", () => {
    const r = checkAndConsumeQuota({
      calls: 10,
      costMicros: 0,
      addCostMicros: 1,
      callCap: 10,
      costCapMicros: usdToMicros(100),
      spendWarnFraction: 0.8,
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, false);
    assert.equal(r.reason, "calls");
    assert.ok((r.retryAfterSeconds ?? 0) > 0);
  });

  it("denies on cost cap", () => {
    const r = checkAndConsumeQuota({
      calls: 0,
      costMicros: usdToMicros(1),
      addCostMicros: 1,
      callCap: 100,
      costCapMicros: usdToMicros(1),
      spendWarnFraction: 0.8,
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, false);
    assert.equal(r.reason, "cost");
  });

  it("flags spend warn when crossing 80% of cost cap", () => {
    const r = checkAndConsumeQuota({
      calls: 0,
      costMicros: usdToMicros(0.75),
      addCostMicros: usdToMicros(0.1),
      callCap: 100,
      costCapMicros: usdToMicros(1),
      spendWarnFraction: 0.8,
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, true);
    assert.equal(r.crossedSpendWarn, true);
  });
});

describe("MemoryStore + enforceQuota", () => {
  let store: MemoryStore;
  let sink: CapturingSink;

  beforeEach(() => {
    store = new MemoryStore();
    sink = new CapturingSink();
  });

  it("returns capacity within ms after call cap — no further consumes", async () => {
    const config = testQuotaConfig({
      dailyCallCap: 3,
      dailyCostCapUsd: 100,
      estUsdPerBrief: 0.01,
    });

    for (let i = 0; i < 3; i += 1) {
      const r = await enforceQuota({
        store,
        config,
        resource: "brief",
        failMode: "closed",
        sink,
        now: FIXED_NOW,
      });
      assert.equal(r.ok, true, `allow ${i}`);
    }

    const t0 = Date.now();
    const denied = await enforceQuota({
      store,
      config,
      resource: "brief",
      failMode: "closed",
      sink,
      now: FIXED_NOW,
    });
    const elapsed = Date.now() - t0;
    assert.ok(elapsed < 50, `capacity check too slow: ${elapsed}ms`);
    assert.equal(denied.ok, false);
    if (!denied.ok) {
      assert.equal(denied.kind, "capacity");
      if (denied.kind === "capacity") {
        assert.equal(denied.reason, "calls");
        const body = capacityBody(denied);
        assert.equal(body.code, "capacity_exhausted");
      }
    }
    assert.ok(sink.payloads.some((p) => p.kind === "quota_hard_cap"));
    assert.ok(
      sink.payloads.some((p) => p.runbook === RUNBOOK_GEMINI_SPEND),
    );
  });

  it("fires spend_warn once before hard cost trip", async () => {
    const config = testQuotaConfig({
      dailyCallCap: 100,
      dailyCostCapUsd: 1,
      estUsdPerGenerate: 0.3,
      spendWarnFraction: 0.8,
    });

    // 0.3 + 0.3 + 0.3 = 0.9 → crosses 0.8 on third call
    for (let i = 0; i < 3; i += 1) {
      const r = await enforceQuota({
        store,
        config,
        resource: "generate",
        failMode: "closed",
        sink,
        now: FIXED_NOW,
      });
      assert.equal(r.ok, true, `allow ${i}`);
    }
    assert.equal(
      sink.payloads.filter((p) => p.kind === "spend_warn").length,
      1,
    );

    // Fourth would be 1.2 > 1.0 — but cost already at 0.9, fifth...
    // Next calls until cost cap: one more at 1.2? nextCost = 0.9+0.3=1.2 which
    // is still allowed if costMicros (0.9) < cap (1.0). Then following denied.
    const fourth = await enforceQuota({
      store,
      config,
      resource: "generate",
      failMode: "closed",
      sink,
      now: FIXED_NOW,
    });
    assert.equal(fourth.ok, true);

    const denied = await enforceQuota({
      store,
      config,
      resource: "generate",
      failMode: "closed",
      sink,
      now: FIXED_NOW,
    });
    assert.equal(denied.ok, false);
    if (!denied.ok) assert.equal(denied.kind, "capacity");
    assert.ok(sink.payloads.some((p) => p.kind === "quota_hard_cap"));
    // Deduped spend_warn
    assert.equal(
      sink.payloads.filter((p) => p.kind === "spend_warn").length,
      1,
    );
  });

  it("resets counters on a new UTC day", async () => {
    const config = testQuotaConfig({
      dailyCallCap: 1,
      dailyCostCapUsd: 100,
      estUsdPerBrief: 0.01,
    });

    const first = await enforceQuota({
      store,
      config,
      resource: "brief",
      failMode: "closed",
      sink,
      now: FIXED_NOW,
    });
    assert.equal(first.ok, true);

    const denied = await enforceQuota({
      store,
      config,
      resource: "brief",
      failMode: "closed",
      sink,
      now: FIXED_NOW,
    });
    assert.equal(denied.ok, false);

    const next = await enforceQuota({
      store,
      config,
      resource: "brief",
      failMode: "closed",
      sink,
      now: NEXT_DAY,
    });
    assert.equal(next.ok, true);
    assert.equal(dayWindowId(NEXT_DAY), "2026-09-02");
    assert.notEqual(
      quotaCallsKey(dayWindowId(FIXED_NOW)),
      quotaCallsKey(dayWindowId(NEXT_DAY)),
    );
  });

  it("fail-closed returns unavailable when store throws", async () => {
    const throwing: RateLimitStore = {
      async get() {
        throw new Error("boom");
      },
      async compareAndSet() {
        throw new Error("boom");
      },
    };
    const denied = await enforceQuota({
      store: throwing,
      config: testQuotaConfig(),
      resource: "brief",
      failMode: "closed",
      sink,
      now: FIXED_NOW,
    });
    assert.equal(denied.ok, false);
    if (!denied.ok) assert.equal(denied.kind, "unavailable");
  });

  it("quota key shape is global (not per-identity)", () => {
    assert.equal(quotaCallsKey("2026-09-01"), "quota/v1/gemini/2026-09-01/calls");
    assert.equal(
      quotaCostMicrosKey("2026-09-01"),
      "quota/v1/gemini/2026-09-01/cost_micros",
    );
  });
});

describe("alerts + ops metrics", () => {
  let store: MemoryStore;
  let sink: CapturingSink;

  beforeEach(() => {
    store = new MemoryStore();
    sink = new CapturingSink();
  });

  it("claimAlertSlot dedupes", async () => {
    const key = alertDedupeKey("spend_warn", "2026-09-01");
    assert.equal(await claimAlertSlot(store, key), true);
    assert.equal(await claimAlertSlot(store, key), false);
  });

  it("fireAlert includes runbook and at timestamp", async () => {
    const fired = await fireAlert(
      {
        kind: "spend_warn",
        severity: "warn",
        message: "test",
        runbook: RUNBOOK_GEMINI_SPEND,
        details: { x: 1 },
      },
      { sink, now: FIXED_NOW },
    );
    assert.equal(fired, true);
    assert.equal(sink.payloads.length, 1);
    assert.equal(sink.payloads[0].runbook, RUNBOOK_GEMINI_SPEND);
    assert.equal(sink.payloads[0].at, new Date(FIXED_NOW).toISOString());
  });

  it("elevated error rate fires alert with runbook", async () => {
    const config = loadOpsAlertConfig({
      ALERT_ERROR_RATE_THRESHOLD: "0.05",
      ALERT_ERROR_RATE_WINDOW_MINUTES: "10",
      ALERT_ERROR_RATE_MIN_SAMPLES: "20",
    });

    for (let i = 0; i < 20; i += 1) {
      await recordOutcome({
        store,
        sink,
        config,
        ok: true,
        now: FIXED_NOW,
      });
    }
    assert.equal(sink.payloads.length, 0);

    // 20 ok + 1 err = 21 samples, rate ≈ 4.8% < 5% — still quiet.
    await recordOutcome({ store, sink, config, ok: false, now: FIXED_NOW });
    assert.equal(sink.payloads.length, 0);

    // 20 ok + 2 err ≈ 9.1% → alert.
    await recordOutcome({ store, sink, config, ok: false, now: FIXED_NOW });
    assert.ok(sink.payloads.some((p) => p.kind === "error_rate"));
    assert.ok(
      sink.payloads.some((p) => p.runbook === RUNBOOK_GEMINI_SPEND),
    );
  });

  it("abuse 429 hammering fires alert with abuse runbook", async () => {
    const config = loadOpsAlertConfig({
      ALERT_ABUSE_429_PER_MINUTE: "5",
    });
    const subject = "ip:203.0.113.9";
    for (let i = 0; i < 4; i += 1) {
      await recordRateLimited({
        store,
        sink,
        config,
        subject,
        now: FIXED_NOW,
      });
    }
    assert.equal(sink.payloads.length, 0);

    await recordRateLimited({
      store,
      sink,
      config,
      subject,
      now: FIXED_NOW,
    });
    assert.ok(sink.payloads.some((p) => p.kind === "abuse_429"));
    assert.ok(sink.payloads.some((p) => p.runbook === RUNBOOK_ABUSE_429));
    assert.equal(
      abuse429Key(subject, minuteWindowId(FIXED_NOW)),
      `metrics/v1/429/${subject}/${minuteWindowId(FIXED_NOW)}`,
    );
  });
});
