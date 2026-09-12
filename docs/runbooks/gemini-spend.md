# Runbook: Gemini spend / capacity

**Alert kinds:** `spend_warn`, `quota_hard_cap`, `error_rate`  
**Code:** `netlify/functions/lib/quota.mts`, `opsMetrics.mts`, `alerts.mts`

## What this means

Global daily Gemini usage is approaching or has hit the hard caps configured
via env (`GEMINI_DAILY_CALL_CAP`, `GEMINI_DAILY_COST_CAP_USD`). Estimated
cost is used (no live billing API) — see `GEMINI_EST_USD_PER_BRIEF` /
`GEMINI_EST_USD_PER_GENERATE`.

- **`spend_warn`** — estimated spend crossed the warn fraction (default 80%)
  of the daily cost cap. Capacity is still available.
- **`quota_hard_cap`** — call or cost cap already reached. `/api/brief` and
  `/api/generate` return **503** `capacity_exhausted` without calling Gemini.
- **`error_rate`** — upstream/assembly failures exceeded the configured
  fraction over the sliding window (default 5% / 10 minutes).

Counters reset automatically at **UTC midnight**.

## Immediate checks

1. Confirm alert payload `details` (`calls`, `cost_usd`, `day`, `rate`).
2. In Netlify → Functions logs, filter `agsynth_alert` JSON lines.
3. Verify site env: caps, estimates, `ALERT_WEBHOOK_URL`,
   `QUOTA_FAIL_MODE` (should stay `closed` in prod).
4. If spend is unexpected: check for a traffic spike, leaked demo abuse
   (see [abuse-429](abuse-429.md)), or underestimate of
   `GEMINI_EST_USD_PER_*`.

## Mitigation

| Situation | Action |
|-----------|--------|
| Legitimate traffic near cap | Raise `GEMINI_DAILY_*` caps temporarily; plan billing/key limits. |
| Cap already tripped | Users see 503 until next UTC day or until caps are raised + redeploy env. |
| High error rate | Check Gemini status, model name envs, function timeouts; roll back recent deploys. |
| Suspected key leak / runaway | Rotate `GEMINI_KEY`; set call cap to a low value; review Netlify deploy logs. |

## Who to ping

- **On-call / repo owner** for env and key rotation.
- **Gemini / Google Cloud billing** if real project spend disagrees with estimates.

## Non-goals

No operator dashboard in this story — alerts + this runbook only.
