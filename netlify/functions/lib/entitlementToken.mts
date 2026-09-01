// ── Compact HS256 entitlement tokens (#312) ─────────────────────────
//
// Server-signed JWT (header.payload.sig). Payload claims only:
//   sub, tier:"paid", exp (unix seconds). No secrets in the payload;
//   ENTITLEMENT_SIGNING_KEY never leaves the server.

export interface SignedEntitlementClaims {
  sub: string;
  tier: "paid";
  /** Unix expiry seconds (JWT `exp`). */
  exp: number;
}

export type TokenVerifyResult =
  | { ok: true; claims: SignedEntitlementClaims }
  | { ok: false; reason: "malformed" | "bad_sig" | "expired" | "claims" };

const JWT_HEADER = base64UrlEncode(
  new TextEncoder().encode(JSON.stringify({ alg: "HS256", typ: "JWT" })),
);

const DEFAULT_TTL_SECONDS = 3600;

export function loadSigningKey(
  env: Record<string, string | undefined> = process.env,
): string | undefined {
  const key = env.ENTITLEMENT_SIGNING_KEY;
  if (typeof key !== "string") return undefined;
  const trimmed = key.trim();
  return trimmed.length > 0 ? trimmed : undefined;
}

export function loadTokenTtlSeconds(
  env: Record<string, string | undefined> = process.env,
): number {
  const raw = env.ENTITLEMENT_TOKEN_TTL_SECONDS;
  if (raw === undefined || raw.trim().length === 0) return DEFAULT_TTL_SECONDS;
  const n = Number(raw);
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 60) return DEFAULT_TTL_SECONDS;
  return n;
}

function base64UrlEncode(bytes: Uint8Array): string {
  let bin = "";
  for (let i = 0; i < bytes.length; i++) {
    bin += String.fromCharCode(bytes[i]!);
  }
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function base64UrlDecode(s: string): Uint8Array | null {
  try {
    const pad = s.length % 4 === 0 ? "" : "=".repeat(4 - (s.length % 4));
    const b64 = s.replace(/-/g, "+").replace(/_/g, "/") + pad;
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  } catch {
    return null;
  }
}

function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a[i]! ^ b[i]!;
  return diff === 0;
}

async function importHmacKey(secret: string): Promise<CryptoKey> {
  return crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"],
  );
}

async function signBytes(secret: string, data: string): Promise<Uint8Array> {
  const key = await importHmacKey(secret);
  const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(data));
  return new Uint8Array(sig);
}

function parseClaims(raw: unknown): SignedEntitlementClaims | null {
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) return null;
  const o = raw as Record<string, unknown>;
  if (typeof o.sub !== "string" || o.sub.trim().length === 0) return null;
  if (o.tier !== "paid") return null;
  if (typeof o.exp !== "number" || !Number.isFinite(o.exp) || !Number.isInteger(o.exp)) {
    return null;
  }
  // Reject unexpected secret-like fields if ever smuggled in.
  if ("key" in o || "secret" in o || "signing_key" in o) return null;
  return { sub: o.sub.trim(), tier: "paid", exp: o.exp };
}

/** Issue a short-lived HS256 JWT. `nowMs` is injectable for tests. */
export async function issueEntitlementToken(
  secret: string,
  subject: string,
  opts: { ttlSeconds?: number; nowMs?: number } = {},
): Promise<{ token: string; claims: SignedEntitlementClaims }> {
  const sub = subject.trim();
  if (sub.length === 0) {
    throw new Error("entitlement subject must be non-empty");
  }
  const nowMs = opts.nowMs ?? Date.now();
  const ttl = opts.ttlSeconds ?? DEFAULT_TTL_SECONDS;
  const claims: SignedEntitlementClaims = {
    sub,
    tier: "paid",
    exp: Math.floor(nowMs / 1000) + ttl,
  };
  const payload = base64UrlEncode(new TextEncoder().encode(JSON.stringify(claims)));
  const signingInput = `${JWT_HEADER}.${payload}`;
  const sig = base64UrlEncode(await signBytes(secret, signingInput));
  return { token: `${signingInput}.${sig}`, claims };
}

/** Verify signature + exp + claim shape. Never throws. */
export async function verifyEntitlementToken(
  secret: string,
  token: string,
  nowMs: number = Date.now(),
): Promise<TokenVerifyResult> {
  const trimmed = token.trim();
  const parts = trimmed.split(".");
  if (parts.length !== 3) return { ok: false, reason: "malformed" };
  const [headerB64, payloadB64, sigB64] = parts as [string, string, string];
  if (headerB64.length === 0 || payloadB64.length === 0 || sigB64.length === 0) {
    return { ok: false, reason: "malformed" };
  }

  const headerBytes = base64UrlDecode(headerB64);
  if (headerBytes === null) return { ok: false, reason: "malformed" };
  let header: unknown;
  try {
    header = JSON.parse(new TextDecoder().decode(headerBytes));
  } catch {
    return { ok: false, reason: "malformed" };
  }
  if (
    typeof header !== "object" ||
    header === null ||
    (header as { alg?: unknown }).alg !== "HS256"
  ) {
    return { ok: false, reason: "malformed" };
  }

  const signingInput = `${headerB64}.${payloadB64}`;
  const expected = await signBytes(secret, signingInput);
  const actual = base64UrlDecode(sigB64);
  if (actual === null || !timingSafeEqual(expected, actual)) {
    return { ok: false, reason: "bad_sig" };
  }

  const payloadBytes = base64UrlDecode(payloadB64);
  if (payloadBytes === null) return { ok: false, reason: "malformed" };
  let raw: unknown;
  try {
    raw = JSON.parse(new TextDecoder().decode(payloadBytes));
  } catch {
    return { ok: false, reason: "malformed" };
  }
  const claims = parseClaims(raw);
  if (claims === null) return { ok: false, reason: "claims" };

  const nowSec = Math.floor(nowMs / 1000);
  if (claims.exp <= nowSec) return { ok: false, reason: "expired" };

  return { ok: true, claims };
}
