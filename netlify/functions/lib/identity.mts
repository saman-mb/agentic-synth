// ── Request identity for tiered rate limiting (#309) ────────────────
//
// Resolution order:
//   1. Authorization: Bearer <token> → EntitlementVerifier
//      - valid paid claims → { tier: paid, subject }
//      - present but invalid/unrecognised → 401 (never fall through)
//   2. X-Device-Id UUID v4 → demo subject `dev:{uuid}`
//   3. else client IP → demo subject `ip:{ip}`

import type { EntitlementVerifier } from "./entitlement.mts";
import type { RateLimitTier } from "./rateLimitConfig.mts";

export interface Identity {
  tier: RateLimitTier;
  subject: string;
}

export type ResolveIdentityResult =
  | { ok: true; identity: Identity }
  | { ok: false; status: 401; error: string };

const UUID_V4 =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

/**
 * Netlify injects x-nf-client-connection-ip at the edge (not
 * client-controllable). x-forwarded-for is only a fallback.
 */
export function clientIp(req: Request): string {
  const nf = req.headers.get("x-nf-client-connection-ip");
  if (nf !== null && nf.trim().length > 0) return nf.trim();
  const fwd = req.headers.get("x-forwarded-for");
  if (fwd !== null) {
    const first = fwd.split(",")[0].trim();
    if (first.length > 0) return first;
  }
  return "unknown";
}

type BearerParse =
  | { kind: "absent" }
  | { kind: "malformed" }
  | { kind: "token"; token: string };

function parseBearer(req: Request): BearerParse {
  const auth = req.headers.get("authorization");
  if (auth === null) return { kind: "absent" };
  const trimmed = auth.trim();
  if (!/^Bearer(\s+|$)/i.test(trimmed)) return { kind: "absent" };
  const token = trimmed.replace(/^Bearer\s+/i, "").trim();
  if (token.length === 0) return { kind: "malformed" };
  return { kind: "token", token };
}

export async function resolveIdentity(
  req: Request,
  verifier: EntitlementVerifier,
): Promise<ResolveIdentityResult> {
  const bearer = parseBearer(req);
  if (bearer.kind === "malformed") {
    return { ok: false, status: 401, error: "Invalid or expired entitlement token." };
  }
  if (bearer.kind === "token") {
    const claims = await verifier.verify(bearer.token);
    if (claims === null) {
      return { ok: false, status: 401, error: "Invalid or expired entitlement token." };
    }
    return { ok: true, identity: { tier: claims.tier, subject: claims.subject } };
  }

  const device = req.headers.get("x-device-id");
  if (device !== null) {
    const id = device.trim();
    if (UUID_V4.test(id)) {
      return { ok: true, identity: { tier: "demo", subject: `dev:${id.toLowerCase()}` } };
    }
  }

  const ip = clientIp(req);
  return { ok: true, identity: { tier: "demo", subject: `ip:${ip}` } };
}
