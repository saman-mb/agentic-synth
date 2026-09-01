// ── Entitlement token + receipt endpoint tests (#312) ───────────────

import { describe, it, beforeEach } from "node:test";
import assert from "node:assert/strict";

import {
  CompositeEntitlementVerifier,
  HmacEntitlementVerifier,
  StubEntitlementVerifier,
  createEntitlementVerifier,
} from "./entitlement.mts";
import {
  issueEntitlementToken,
  verifyEntitlementToken,
  loadSigningKey,
} from "./entitlementToken.mts";
import { validateReceipt, loadReceiptMode } from "./receiptValidate.mts";
import { resolveIdentity } from "./identity.mts";
import { enforceRateLimit } from "./rateLimit.mts";
import { DEFAULT_DEMO_LIMITS, DEFAULT_PAID_LIMITS } from "./rateLimitConfig.mts";
import { MemoryStore } from "./rateLimitStore.mts";
import { handleEntitlement } from "../entitlement.mts";
import { handleBrief } from "../brief.mts";
import {
  resetRateLimitRuntime,
  setRateLimitRuntime,
} from "./rateLimitGate.mts";

const SECRET = "test-signing-key-not-for-production-use!!";
const FIXED_NOW = Date.UTC(2026, 8, 1, 12, 0, 0); // 2026-09-01T12:00:00Z

describe("entitlementToken HMAC", () => {
  it("issues a JWT with sub/tier/exp and no secret fields", async () => {
    const { token, claims } = await issueEntitlementToken(SECRET, "ios:user-1", {
      ttlSeconds: 3600,
      nowMs: FIXED_NOW,
    });
    assert.equal(claims.sub, "ios:user-1");
    assert.equal(claims.tier, "paid");
    assert.equal(claims.exp, Math.floor(FIXED_NOW / 1000) + 3600);
    assert.equal(token.split(".").length, 3);
    // Payload must not contain the signing key.
    const payloadB64 = token.split(".")[1]!;
    const pad = payloadB64.length % 4 === 0 ? "" : "=".repeat(4 - (payloadB64.length % 4));
    const json = Buffer.from(payloadB64.replace(/-/g, "+").replace(/_/g, "/") + pad, "base64").toString(
      "utf8",
    );
    assert.equal(json.includes(SECRET), false);
    assert.equal(json.includes("signing"), false);
  });

  it("verifies a valid token", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:ok", {
      ttlSeconds: 60,
      nowMs: FIXED_NOW,
    });
    const r = await verifyEntitlementToken(SECRET, token, FIXED_NOW + 1000);
    assert.equal(r.ok, true);
    if (r.ok) assert.equal(r.claims.sub, "ios:ok");
  });

  it("rejects tampered payload", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:ok", {
      ttlSeconds: 3600,
      nowMs: FIXED_NOW,
    });
    const parts = token.split(".");
    const badPayload = Buffer.from(JSON.stringify({ sub: "ios:evil", tier: "paid", exp: 9999999999 }))
      .toString("base64url");
    const tampered = `${parts[0]}.${badPayload}.${parts[2]}`;
    const r = await verifyEntitlementToken(SECRET, tampered, FIXED_NOW);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.reason, "bad_sig");
  });

  it("rejects expired token", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:old", {
      ttlSeconds: 60,
      nowMs: FIXED_NOW,
    });
    const r = await verifyEntitlementToken(SECRET, token, FIXED_NOW + 120_000);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.reason, "expired");
  });

  it("loadSigningKey trims and rejects empty/short keys", () => {
    const longKey = "a".repeat(32);
    assert.equal(loadSigningKey({ ENTITLEMENT_SIGNING_KEY: `  ${longKey}  ` }), longKey);
    assert.equal(loadSigningKey({ ENTITLEMENT_SIGNING_KEY: "  " }), undefined);
    assert.equal(loadSigningKey({ ENTITLEMENT_SIGNING_KEY: "too-short" }), undefined);
    assert.equal(loadSigningKey({}), undefined);
  });
});

describe("HmacEntitlementVerifier + identity", () => {
  it("valid token → paid identity", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:alice", {
      ttlSeconds: 3600,
      nowMs: FIXED_NOW,
    });
    const verifier = new HmacEntitlementVerifier(SECRET, () => FIXED_NOW);
    const req = new Request("https://example.test/api/brief", {
      headers: { Authorization: `Bearer ${token}` },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.identity.tier, "paid");
      assert.equal(r.identity.subject, "paid:ios:alice");
    }
  });

  it("expired/tampered Bearer → 401 (no demo fallthrough)", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:alice", {
      ttlSeconds: 1,
      nowMs: FIXED_NOW,
    });
    const verifier = new HmacEntitlementVerifier(SECRET, () => FIXED_NOW + 60_000);
    const req = new Request("https://example.test/api/brief", {
      headers: {
        Authorization: `Bearer ${token}`,
        "x-nf-client-connection-ip": "1.1.1.1",
      },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 401);
  });

  it("valid token → paid-tier rate limits apply", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:rate", {
      ttlSeconds: 3600,
      nowMs: FIXED_NOW,
    });
    const verifier = new HmacEntitlementVerifier(SECRET, () => FIXED_NOW);
    const req = new Request("https://example.test/api/brief", {
      headers: { Authorization: `Bearer ${token}` },
    });
    const id = await resolveIdentity(req, verifier);
    assert.equal(id.ok, true);
    if (!id.ok) return;

    const store = new MemoryStore();
    // Consume demo limit + 1 would deny demo; paid allows more.
    for (let i = 0; i < DEFAULT_DEMO_LIMITS.minute; i++) {
      const r = await enforceRateLimit({
        store,
        identity: id.identity,
        resource: "brief",
        limits: DEFAULT_PAID_LIMITS,
        failMode: "closed",
        now: FIXED_NOW,
      });
      assert.equal(r.ok, true);
    }
    // Still under paid minute (30).
    const still = await enforceRateLimit({
      store,
      identity: id.identity,
      resource: "brief",
      limits: DEFAULT_PAID_LIMITS,
      failMode: "closed",
      now: FIXED_NOW,
    });
    assert.equal(still.ok, true);
  });
});

describe("createEntitlementVerifier wiring", () => {
  it("uses HMAC when signing key is set", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:w", {
      ttlSeconds: 3600,
      nowMs: FIXED_NOW,
    });
    const v = createEntitlementVerifier(
      { paidStubTokens: new Set(), allowPaidStubPrefix: false },
      { ENTITLEMENT_SIGNING_KEY: SECRET },
      () => FIXED_NOW,
    );
    assert.ok(v instanceof HmacEntitlementVerifier);
    const claims = await v.verify(token);
    assert.deepEqual(claims, { subject: "paid:ios:w", tier: "paid" });
  });

  it("falls back to stub when no signing key", async () => {
    const v = createEntitlementVerifier(
      { paidStubTokens: new Set(["local-tok"]), allowPaidStubPrefix: false },
      {},
    );
    assert.ok(v instanceof StubEntitlementVerifier);
    const claims = await v.verify("local-tok");
    assert.equal(claims?.tier, "paid");
  });

  it("composites HMAC + stub when stub flags on with key", async () => {
    const v = createEntitlementVerifier(
      { paidStubTokens: new Set(), allowPaidStubPrefix: true },
      { ENTITLEMENT_SIGNING_KEY: SECRET },
      () => FIXED_NOW,
    );
    assert.ok(v instanceof CompositeEntitlementVerifier);
    const stubClaims = await v.verify("paidstub.local-user");
    assert.equal(stubClaims?.subject, "paid:local-user");
  });
  it("malformed Bearer header → 401 (no demo fallthrough)", async () => {
    const verifier = new HmacEntitlementVerifier(SECRET, () => FIXED_NOW);
    const req = new Request("https://example.test/api/brief", {
      headers: { Authorization: "Bearer" },
    });
    const r = await resolveIdentity(req, verifier);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 401);
  });
});

describe("receiptValidate", () => {
  it("default mode is unconfigured (fail closed)", async () => {
    assert.equal(loadReceiptMode({}), "unconfigured");
    const r = await validateReceipt(
      { receipt: "test:mobile-1", platform: "ios" },
      { ENTITLEMENT_SIGNING_KEY: SECRET },
    );
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 503);
  });

  it("stub mode requires ENTITLEMENT_ALLOW_STUB_RECEIPTS=1", async () => {
    assert.equal(
      loadReceiptMode({ ENTITLEMENT_RECEIPT_MODE: "stub" }),
      "unconfigured",
    );
    const r = await validateReceipt(
      { receipt: "test:mobile-1", platform: "ios" },
      { ENTITLEMENT_RECEIPT_MODE: "stub", ENTITLEMENT_SIGNING_KEY: SECRET },
    );
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 503);
  });

  it("stub mode accepts test: receipts when explicitly enabled", async () => {
    assert.equal(
      loadReceiptMode({
        ENTITLEMENT_RECEIPT_MODE: "stub",
        ENTITLEMENT_ALLOW_STUB_RECEIPTS: "1",
      }),
      "stub",
    );
    const r = await validateReceipt(
      { receipt: "test:mobile-1", platform: "ios" },
      {
        ENTITLEMENT_RECEIPT_MODE: "stub",
        ENTITLEMENT_ALLOW_STUB_RECEIPTS: "1",
      },
    );
    assert.equal(r.ok, true);
    if (r.ok) assert.equal(r.subject, "ios:mobile-1");
  });

  it("stub mode rejects unknown receipts with 403", async () => {
    const r = await validateReceipt(
      { receipt: "not-a-test-receipt", platform: "ios" },
      {
        ENTITLEMENT_RECEIPT_MODE: "stub",
        ENTITLEMENT_ALLOW_STUB_RECEIPTS: "1",
      },
    );
    assert.equal(r.ok, false);
    if (!r.ok) {
      assert.equal(r.status, 403);
      assert.notEqual(r.status, 500);
    }
  });

  it("apple mode without secret → 503, no subject", async () => {
    const r = await validateReceipt(
      { receipt: "real-looking-receipt", platform: "ios" },
      { ENTITLEMENT_RECEIPT_MODE: "apple" },
    );
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 503);
  });

  it("apple mode fetch failure → 503 (fail closed)", async () => {
    const boom = async () => {
      throw new Error("network down");
    };
    const r = await validateReceipt(
      { receipt: "real-looking-receipt", platform: "ios" },
      {
        ENTITLEMENT_RECEIPT_MODE: "apple",
        APPLE_SHARED_SECRET: "sec",
        APPLE_BUNDLE_ID: "com.example.app",
      },
      boom as unknown as typeof fetch,
    );
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 503);
  });

  it("apple mode validates bundle, product, and active entitlement", async () => {
    const mockFetch = async () =>
      new Response(
        JSON.stringify({
          status: 0,
          receipt: { bundle_id: "com.example.app" },
          latest_receipt_info: [
            {
              original_transaction_id: "txn-active-1",
              product_id: "com.example.pro",
              expires_date_ms: String(FIXED_NOW + 86_400_000),
            },
          ],
        }),
        { status: 200, headers: { "Content-Type": "application/json" } },
      );
    const r = await validateReceipt(
      { receipt: "base64-receipt", platform: "ios" },
      {
        ENTITLEMENT_RECEIPT_MODE: "apple",
        APPLE_SHARED_SECRET: "sec",
        APPLE_BUNDLE_ID: "com.example.app",
        APPLE_PRODUCT_ID: "com.example.pro",
      },
      mockFetch as unknown as typeof fetch,
    );
    assert.equal(r.ok, true);
    if (r.ok) assert.equal(r.subject, "ios:txn-active-1");
  });

  it("apple mode rejects wrong bundle_id", async () => {
    const mockFetch = async () =>
      new Response(
        JSON.stringify({
          status: 0,
          receipt: { bundle_id: "com.other.app" },
          latest_receipt_info: [
            {
              original_transaction_id: "txn-1",
              product_id: "com.example.pro",
              expires_date_ms: String(FIXED_NOW + 86_400_000),
            },
          ],
        }),
        { status: 200 },
      );
    const r = await validateReceipt(
      { receipt: "base64-receipt", platform: "ios" },
      {
        ENTITLEMENT_RECEIPT_MODE: "apple",
        APPLE_SHARED_SECRET: "sec",
        APPLE_BUNDLE_ID: "com.example.app",
      },
      mockFetch as unknown as typeof fetch,
    );
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.status, 403);
  });
});

describe("POST /api/entitlement", () => {
  it("issues a token for a stub receipt", async () => {
    const req = new Request("https://example.test/api/entitlement", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ receipt: "test:ship-312", platform: "ios" }),
    });
    const res = await handleEntitlement(req, {
      env: {
        ENTITLEMENT_SIGNING_KEY: SECRET,
        ENTITLEMENT_RECEIPT_MODE: "stub",
        ENTITLEMENT_ALLOW_STUB_RECEIPTS: "1",
        ENTITLEMENT_TOKEN_TTL_SECONDS: "3600",
      },
      gateRateLimit: async () => null,
      nowMs: () => FIXED_NOW,
    });
    assert.equal(res.status, 200);
    const body = (await res.json()) as {
      token: string;
      expires_at: number;
      tier: string;
    };
    assert.equal(body.tier, "paid");
    assert.equal(body.expires_at, Math.floor(FIXED_NOW / 1000) + 3600);
    assert.ok(typeof body.token === "string" && body.token.split(".").length === 3);

    const verified = await verifyEntitlementToken(SECRET, body.token, FIXED_NOW);
    assert.equal(verified.ok, true);
    if (verified.ok) assert.equal(verified.claims.sub, "ios:ship-312");
  });

  it("missing signing key → 503", async () => {
    const req = new Request("https://example.test/api/entitlement", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ receipt: "test:x", platform: "ios" }),
    });
    const res = await handleEntitlement(req, {
      env: { ENTITLEMENT_RECEIPT_MODE: "stub", ENTITLEMENT_ALLOW_STUB_RECEIPTS: "1" },
      gateRateLimit: async () => null,
    });
    assert.equal(res.status, 503);
    const body = (await res.json()) as { error: string };
    assert.match(body.error, /ENTITLEMENT_SIGNING_KEY/);
  });

  it("invalid receipt → 403, never 5xx", async () => {
    const req = new Request("https://example.test/api/entitlement", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ receipt: "bogus", platform: "ios" }),
    });
    const res = await handleEntitlement(req, {
      env: {
        ENTITLEMENT_SIGNING_KEY: SECRET,
        ENTITLEMENT_RECEIPT_MODE: "stub",
        ENTITLEMENT_ALLOW_STUB_RECEIPTS: "1",
      },
      gateRateLimit: async () => null,
    });
    assert.equal(res.status, 403);
  });
});

describe("anonymous brief path unchanged (additive)", () => {
  beforeEach(() => {
    resetRateLimitRuntime();
  });

  it("POST /api/brief without Bearer still works as demo", async () => {
    setRateLimitRuntime({
      config: {
        demo: DEFAULT_DEMO_LIMITS,
        paid: DEFAULT_PAID_LIMITS,
        failMode: "closed",
        store: "memory",
        paidStubTokens: new Set(),
        allowPaidStubPrefix: false,
      },
      verifier: new HmacEntitlementVerifier(SECRET, () => FIXED_NOW),
      store: new MemoryStore(),
    });

    const res = await handleBrief(
      new Request("https://example.test/api/brief", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "x-nf-client-connection-ip": "198.51.100.10",
        },
        body: JSON.stringify({ prompt: "warm pad" }),
      }),
      {
        getApiKey: () => "fake-key",
        enhanceBrief: async (p) => `brief:${p}`,
        gateQuota: async () => null,
        noteOutcome: async () => {},
      },
    );
    assert.equal(res.status, 200);
    const body = (await res.json()) as { brief: string };
    assert.match(body.brief, /warm pad/);
  });

  it("Bearer with expired HMAC token → 401 from brief", async () => {
    const { token } = await issueEntitlementToken(SECRET, "ios:gone", {
      ttlSeconds: 1,
      nowMs: FIXED_NOW,
    });
    setRateLimitRuntime({
      config: {
        demo: DEFAULT_DEMO_LIMITS,
        paid: DEFAULT_PAID_LIMITS,
        failMode: "closed",
        store: "memory",
        paidStubTokens: new Set(),
        allowPaidStubPrefix: false,
      },
      verifier: new HmacEntitlementVerifier(SECRET, () => FIXED_NOW + 120_000),
      store: new MemoryStore(),
    });

    const res = await handleBrief(
      new Request("https://example.test/api/brief", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`,
        },
        body: JSON.stringify({ prompt: "warm pad" }),
      }),
      {
        getApiKey: () => "fake-key",
        enhanceBrief: async () => {
          throw new Error("should not call Gemini");
        },
      },
    );
    assert.equal(res.status, 401);
  });
});
