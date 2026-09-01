// ── Entitlement verification (#309 seams; #312 HMAC) ────────────────
//
// Resolves a Bearer token to a paid-tier subject, or null when the token
// is absent/unrecognised. Production path: HmacEntitlementVerifier when
// ENTITLEMENT_SIGNING_KEY is set. StubEntitlementVerifier remains for
// local/CI allowlists (RATE_LIMIT_PAID_STUB_TOKENS /
// RATE_LIMIT_ALLOW_PAID_STUB) when no signing key is configured, or as
// a secondary check behind HMAC when those stub flags are explicitly on.

import type { RateLimitConfig } from "./rateLimitConfig.mts";
import {
  loadSigningKey,
  verifyEntitlementToken,
} from "./entitlementToken.mts";

export interface EntitlementClaims {
  /** Stable subject id used in rate-limit store keys. */
  subject: string;
  tier: "paid";
}

export interface EntitlementVerifier {
  verify(token: string): Promise<EntitlementClaims | null>;
}

const PAID_STUB_PREFIX = "paidstub.";

/**
 * Default local / no-key verifier. Empty allowlist + stub flag off →
 * every Bearer token is rejected (caller maps null → 401).
 */
export class StubEntitlementVerifier implements EntitlementVerifier {
  private readonly cfg: Pick<RateLimitConfig, "paidStubTokens" | "allowPaidStubPrefix">;

  constructor(cfg: Pick<RateLimitConfig, "paidStubTokens" | "allowPaidStubPrefix">) {
    this.cfg = cfg;
  }

  async verify(token: string): Promise<EntitlementClaims | null> {
    const trimmed = token.trim();
    if (trimmed.length === 0) return null;

    if (this.cfg.paidStubTokens.has(trimmed)) {
      return { subject: `paid:token:${trimmed}`, tier: "paid" };
    }

    if (this.cfg.allowPaidStubPrefix && trimmed.startsWith(PAID_STUB_PREFIX)) {
      const sub = trimmed.slice(PAID_STUB_PREFIX.length).trim();
      if (sub.length === 0) return null;
      return { subject: `paid:${sub}`, tier: "paid" };
    }

    return null;
  }
}

/** HS256 JWT verifier. Invalid/expired/tampered → null (→ 401 upstream). */
export class HmacEntitlementVerifier implements EntitlementVerifier {
  private readonly secret: string;
  private readonly nowMs: () => number;

  constructor(secret: string, nowMs: () => number = () => Date.now()) {
    this.secret = secret;
    this.nowMs = nowMs;
  }

  async verify(token: string): Promise<EntitlementClaims | null> {
    const result = await verifyEntitlementToken(this.secret, token, this.nowMs());
    if (!result.ok) return null;
    return { subject: `paid:${result.claims.sub}`, tier: "paid" };
  }
}

/** Try verifiers in order; first non-null claims win. */
export class CompositeEntitlementVerifier implements EntitlementVerifier {
  private readonly verifiers: readonly EntitlementVerifier[];

  constructor(verifiers: readonly EntitlementVerifier[]) {
    this.verifiers = verifiers;
  }

  async verify(token: string): Promise<EntitlementClaims | null> {
    for (const v of this.verifiers) {
      const claims = await v.verify(token);
      if (claims !== null) return claims;
    }
    return null;
  }
}

/** Test double: map exact tokens → claims (or null). */
export class FakeEntitlementVerifier implements EntitlementVerifier {
  private readonly byToken: ReadonlyMap<string, EntitlementClaims | null>;

  constructor(byToken: ReadonlyMap<string, EntitlementClaims | null>) {
    this.byToken = byToken;
  }

  async verify(token: string): Promise<EntitlementClaims | null> {
    if (!this.byToken.has(token)) return null;
    return this.byToken.get(token) ?? null;
  }
}

/**
 * Production wiring: HMAC when ENTITLEMENT_SIGNING_KEY is set; otherwise
 * stub allowlist only. When a signing key is set *and* stub flags are on
 * (local only), stub is tried after HMAC so paidstub.* still works.
 */
export function createEntitlementVerifier(
  cfg: Pick<RateLimitConfig, "paidStubTokens" | "allowPaidStubPrefix">,
  env: Record<string, string | undefined> = process.env,
  nowMs: () => number = () => Date.now(),
): EntitlementVerifier {
  const key = loadSigningKey(env);
  const stubEnabled =
    cfg.allowPaidStubPrefix || cfg.paidStubTokens.size > 0;
  const stub = new StubEntitlementVerifier(cfg);

  if (key !== undefined) {
    const hmac = new HmacEntitlementVerifier(key, nowMs);
    if (stubEnabled) {
      return new CompositeEntitlementVerifier([hmac, stub]);
    }
    return hmac;
  }

  return stub;
}
