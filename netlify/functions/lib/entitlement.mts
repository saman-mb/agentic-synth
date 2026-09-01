// ── Entitlement verification (#309 stub; real verify = #312) ────────
//
// Resolves a Bearer token to a paid-tier subject, or null when the token
// is absent/unrecognised. Production StubEntitlementVerifier always
// returns null unless optional test allowlists are configured
// (RATE_LIMIT_PAID_STUB_TOKENS / RATE_LIMIT_ALLOW_PAID_STUB) — residual
// until App Store receipt JWT verify lands in #312.

import type { RateLimitConfig } from "./rateLimitConfig.mts";

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
 * Default production verifier. Empty allowlist + stub flag off → every
 * Bearer token is rejected (caller maps null → 401). Non-prod can set
 * RATE_LIMIT_PAID_STUB_TOKENS or RATE_LIMIT_ALLOW_PAID_STUB=1 so AC
 * "valid entitlement → paid tier" is demonstrable without App Store.
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
