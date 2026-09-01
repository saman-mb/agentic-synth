// ── Rate-limit unit tests (#309) ────────────────────────────────────
//
// MemoryStore + injected clock. Covers demo deny, paid-stub higher
// limit, fail-closed, retryAfter.
//
// Manual load check (optional, not CI): with RATE_LIMIT_STORE=memory and
// a mocked Gemini, fire N concurrent POSTs to /api/brief; Gemini
// invocation count must stay ≤ configured minute/day ceiling. Prefer
// repeating the MemoryStore burst test below over a flaky live script.

import { describe, it, beforeEach } from "node:test";
import assert from "node:assert/strict";

import { StubEntitlementVerifier, FakeEntitlementVerifier } from "./entitlement.mts";
import { resolveIdentity } from "./identity.mts";
import {
  checkAndConsume,
  enforceRateLimit,
  rateLimitedBody,
  retryAfterSeconds,
} from "./rateLimit.mts";
import { loadRateLimitConfig, DEFAULT_DEMO_LIMITS, DEFAULT_PAID_LIMITS } from "./rateLimitConfig.mts";
import { MemoryStore, storeKey, type RateLimitStore } from "./rateLimitStore.mts";

const FIXED_NOW = Date.UTC(2026, 8, 1, 12, 0, 30); // 2026-09-01T12:00:30.000Z

describe("loadRateLimitConfig", () => {
  it("applies demo/paid defaults and fail-closed blobs store", () => {
    const cfg = loadRateLimitConfig({});
    assert.deepEqual(cfg.demo, DEFAULT_DEMO_LIMITS);
    assert.deepEqual(cfg.paid, DEFAULT_PAID_LIMITS);
    assert.equal(cfg.failMode, "closed");
    assert.equal(cfg.store, "blobs");
    assert.equal(cfg.paidStubTokens.size, 0);
    assert.equal(cfg.allowPaidStubPrefix, false);
  });

  it("parses stub allowlist and store=memory", () => {
    const cfg = loadRateLimitConfig({
      RATE_LIMIT_STORE: "memory",
      RATE_LIMIT_FAIL_MODE: "open",
      RATE_LIMIT_PAID_STUB_TOKENS: " tok-a ,tok-b ",
      RATE_LIMIT_ALLOW_PAID_STUB: "1",
      RATE_LIMIT_DEMO_MINUTE: "5",
    });
    assert.equal(cfg.store, "memory");
    assert.equal(cfg.failMode, "open");
    assert.equal(cfg.demo.minute, 5);
    assert.ok(cfg.paidStubTokens.has("tok-a"));
    assert.ok(cfg.paidStubTokens.has("tok-b"));
    assert.equal(cfg.allowPaidStubPrefix, true);
  });
});

describe("checkAndConsume (pure)", () => {
  it("allows under limit and increments both windows", () => {
    const r = checkAndConsume({
      minuteCount: 0,
      dayCount: 0,
      limits: { minute: 3, day: 200 },
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, true);
    assert.equal(r.nextMinuteCount, 1);
    assert.equal(r.nextDayCount, 1);
    assert.equal(r.dayWindowId, "2026-09-01");
  });

  it("denies on minute ceiling with retryAfter into next bucket", () => {
    const r = checkAndConsume({
      minuteCount: 3,
      dayCount: 10,
      limits: { minute: 3, day: 200 },
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, false);
    assert.equal(r.window, "minute");
    assert.equal(r.retryAfterSeconds, retryAfterSeconds("minute", FIXED_NOW));
    assert.equal(r.retryAfterSeconds, 30);
  });

  it("denies on day ceiling preferentially", () => {
    const r = checkAndConsume({
      minuteCount: 0,
      dayCount: 200,
      limits: { minute: 3, day: 200 },
      now: FIXED_NOW,
    });
    assert.equal(r.allowed, false);
    assert.equal(r.window, "day");
    assert.ok((r.retryAfterSeconds ?? 0) > 0);
  });
});

describe("MemoryStore + enforceRateLimit", () => {
  let store: MemoryStore;

  beforeEach(() => {
    store = new MemoryStore();
  });

  it("demo subject is denied after minute limit (3)", async () => {
    const identity = { tier: "demo" as const, subject: "ip:1.2.3.4" };
    const limits = { minute: 3, day: 200 };

    for (let i = 0; i < 3; i += 1) {
      const r = await enforceRateLimit({
        store,
        identity,
        resource: "brief",
        limits,
        failMode: "closed",
        now: FIXED_NOW,
      });
      assert.equal(r.ok, true, `attempt ${i} should allow`);
    }

    const denied = await enforceRateLimit({
      store,
      identity,
      resource: "brief",
      limits,
      failMode: "closed",
      now: FIXED_NOW,
    });
    assert.equal(denied.ok, false);
    if (!denied.ok) {
      assert.equal(denied.kind, "rate_limited");
      if (denied.kind === "rate_limited") {
        assert.equal(denied.window, "minute");
        assert.equal(denied.tier, "demo");
        assert.equal(denied.retryAfterSeconds, 30);
        const body = rateLimitedBody(denied);
        assert.equal(body.code, "rate_limited");
        assert.equal(body.retry_after_seconds, 30);
      }
    }
  });

  it("paid stub subject gets higher minute ceiling (30)", async () => {
    const identity = { tier: "paid" as const, subject: "paid:alice" };
    const limits = DEFAULT_PAID_LIMITS;

    for (let i = 0; i < 30; i += 1) {
      const r = await enforceRateLimit({
        store,
        identity,
        resource: "generate",
        limits,
        failMode: "closed",
        now: FIXED_NOW,
      });
      assert.equal(r.ok, true, `paid attempt ${i}`);
    }

    const denied = await enforceRateLimit({
      store,
      identity,
      resource: "generate",
      limits,
      failMode: "closed",
      now: FIXED_NOW,
    });
    assert.equal(denied.ok, false);
    if (!denied.ok && denied.kind === "rate_limited") {
      assert.equal(denied.window, "minute");
      assert.equal(denied.tier, "paid");
    }
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
    const denied = await enforceRateLimit({
      store: throwing,
      identity: { tier: "demo", subject: "ip:9.9.9.9" },
      resource: "brief",
      limits: DEFAULT_DEMO_LIMITS,
      failMode: "closed",
      now: FIXED_NOW,
    });
    assert.equal(denied.ok, false);
    if (!denied.ok) assert.equal(denied.kind, "unavailable");
  });

  it("fail-open allows when store throws", async () => {
    const throwing: RateLimitStore = {
      async get() {
        throw new Error("boom");
      },
      async compareAndSet() {
        throw new Error("boom");
      },
    };
    const r = await enforceRateLimit({
      store: throwing,
      identity: { tier: "demo", subject: "ip:9.9.9.9" },
      resource: "brief",
      limits: DEFAULT_DEMO_LIMITS,
      failMode: "open",
      now: FIXED_NOW,
    });
    assert.equal(r.ok, true);
  });

  it("storeKey shape matches rl/v1/... contract", () => {
    assert.equal(
      storeKey("demo", "ip:1.2.3.4", "brief", "minute", "123"),
      "rl/v1/demo/ip:1.2.3.4/brief/minute/123",
    );
  });
});

describe("identity + entitlement stub", () => {
  it("Bearer with stub allowlist → paid tier", async () => {
    const verifier = new StubEntitlementVerifier({
      paidStubTokens: new Set(["secret-paid"]),
      allowPaidStubPrefix: false,
    });
    const req = new Request("https://example.test/api/brief", {
      headers: { Authorization: "Bearer secret-paid" },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.identity.tier, "paid");
      assert.equal(r.identity.subject, "paid:token:secret-paid");
    }
  });

  it("Bearer with paidstub. prefix when enabled → paid subject", async () => {
    const verifier = new StubEntitlementVerifier({
      paidStubTokens: new Set(),
      allowPaidStubPrefix: true,
    });
    const req = new Request("https://example.test/api/brief", {
      headers: { Authorization: "Bearer paidstub.mobile-user-1" },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.identity.tier, "paid");
      assert.equal(r.identity.subject, "paid:mobile-user-1");
    }
  });

  it("Bearer present but unverified → 401 (no demo fallthrough)", async () => {
    const verifier = new StubEntitlementVerifier({
      paidStubTokens: new Set(),
      allowPaidStubPrefix: false,
    });
    const req = new Request("https://example.test/api/brief", {
      headers: {
        Authorization: "Bearer junk",
        "x-nf-client-connection-ip": "8.8.8.8",
      },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 401);
  });

  it("X-Device-Id UUID v4 → demo dev: subject", async () => {
    const verifier = new FakeEntitlementVerifier(new Map());
    const uuid = "550e8400-e29b-41d4-a716-446655440000";
    const req = new Request("https://example.test/api/brief", {
      headers: { "X-Device-Id": uuid },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.identity.tier, "demo");
      assert.equal(r.identity.subject, `dev:${uuid}`);
    }
  });

  it("falls back to IP subject", async () => {
    const verifier = new FakeEntitlementVerifier(new Map());
    const req = new Request("https://example.test/api/brief", {
      headers: { "x-nf-client-connection-ip": "203.0.113.9" },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.identity.subject, "ip:203.0.113.9");
    }
  });
});
