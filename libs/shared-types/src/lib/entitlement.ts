/** Entitlement token wire types (#312). Consumed by mobile (#294). */

/** Paid-tier claim embedded in the signed entitlement JWT. */
export type EntitlementTier = "paid";

/**
 * JWT payload claims. Signing key is server-side only — never put secrets
 * in this payload.
 */
export interface EntitlementTokenClaims {
  /** Stable subject (e.g. `ios:<txn>` or stub `ios:test-user`). */
  sub: string;
  tier: EntitlementTier;
  /** Unix expiry seconds. */
  exp: number;
}

/** POST /api/entitlement request body. */
export interface EntitlementReceiptRequest {
  /** Store receipt blob, or stub `test:<subject>` when ENTITLEMENT_RECEIPT_MODE=stub. */
  receipt: string;
  platform?: "ios" | "android";
}

/** Successful POST /api/entitlement response. */
export interface EntitlementTokenResponse {
  token: string;
  /** Unix expiry seconds (matches JWT `exp`). */
  expires_at: number;
  tier: EntitlementTier;
}
