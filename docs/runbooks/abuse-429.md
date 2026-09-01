# Runbook: Identity hammering rate limits (429)

**Alert kind:** `abuse_429`  
**Code:** `netlify/functions/lib/opsMetrics.mts`, `rateLimitGate.mts`

## What this means

A single identity (paid subject, `dev:{uuid}`, or `ip:{addr}`) is receiving
many **429** rate-limit responses inside one UTC minute — above
`ALERT_ABUSE_429_PER_MINUTE` (default 30). Per-identity limits still hold
(#309); this alert makes the hammering visible.

## Immediate checks

1. Read alert `details.subject`, `count`, and `minute`.
2. Correlate with Netlify function 429 logs for `/api/brief` and
   `/api/generate`.
3. Distinguish:
   - **Misbehaving client / retry storm** — same device or IP looping.
   - **Shared NAT** — many users behind one IP (`ip:…` subjects).
   - **Scripted abuse** — high volume, many subjects, or paid-stub misuse.

## Mitigation

| Situation | Action |
|-----------|--------|
| Single scripted IP | Block / challenge at Netlify edge or CDN if available; keep fail-closed rate limits. |
| Shared NAT false positive | Raise `ALERT_ABUSE_429_PER_MINUTE` slightly; prefer `X-Device-Id` clients. |
| Compromised paid stub token | Rotate `RATE_LIMIT_PAID_STUB_TOKENS`; disable `RATE_LIMIT_ALLOW_PAID_STUB` in prod. |
| Global spend also climbing | Follow [gemini-spend](gemini-spend.md); lower daily caps. |

## Who to ping

- **On-call / repo owner** for env and edge rules.
- Escalate to platform/security if traffic looks like a coordinated scrape.

## Related

- Rate-limit policy: `netlify/functions/lib/rateLimit.mts`
- Global capacity: [gemini-spend](gemini-spend.md)
