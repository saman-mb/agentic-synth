# Entitlement tokens (receipt → JWT)

**Code:** `netlify/functions/entitlement.mts`, `lib/entitlementToken.mts`,
`lib/receiptValidate.mts`, `lib/entitlement.mts`  
**Related:** rate limits (#309), mobile consume (#294)

## What this is

Paying mobile clients `POST /api/entitlement` with a store receipt. The
server validates the receipt and returns a short-lived **HS256 JWT**
(`Authorization: Bearer …` on `/api/brief` and `/api/generate`).

Claims in the token (and only these): `sub`, `tier:"paid"`, `exp`.
The signing key (`ENTITLEMENT_SIGNING_KEY`) is **server-side only** —
never embed secrets in the payload.

Anonymous web demo does not use this path; behaviour is unchanged.

## Env

| Variable | Role |
|----------|------|
| `ENTITLEMENT_SIGNING_KEY` | Required to **issue** tokens. When set, Bearer JWTs are verified for paid tier. Missing on issue → **503**. |
| `ENTITLEMENT_TOKEN_TTL_SECONDS` | Default `3600`. |
| `ENTITLEMENT_RECEIPT_MODE` | `stub` (default) or `apple`. |
| `APPLE_SHARED_SECRET` | Required for `apple` mode live `verifyReceipt`. |
| `RATE_LIMIT_PAID_STUB_*` | Local-only allowlist when no signing key (or with key + stub flags for local). Leave unset in prod. |

## Receipt modes

- **stub** — accepts `{"receipt":"test:<subject>","platform":"ios"|"android"}`.
  No network. Use in CI and local.
- **apple** — calls Apple `verifyReceipt`. Unconfigured / unreachable →
  **503** with a clear error and **no** token (fail closed). Invalid
  receipt → **401/403**, never 5xx. Android Play verify is residual
  (503 until wired).

## Status map

| Situation | Status |
|-----------|--------|
| Valid receipt | 200 `{ token, expires_at, tier }` |
| Bad / rejected receipt | 401 / 403 |
| Expired / tampered Bearer on brief/generate | 401 |
| Missing signing key (issue) | 503 |
| Apple/Play dependency down or unconfigured | 503 |

## Residual

- Live Apple network mock in CI; production shared-secret rotation runbook.
- Google Play Billing verify.
- Mobile client (#294) stores and refreshes the JWT.
