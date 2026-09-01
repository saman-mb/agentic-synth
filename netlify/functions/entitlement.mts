// ── POST /api/entitlement — receipt → signed token (#312) ────────────
//
// Mobile clients POST a store receipt; the server validates it and
// returns a short-lived HS256 JWT (sub, tier:"paid", exp). Anonymous
// web demo does not call this path — additive only.
//
// Status map:
//   200 — token issued
//   400 — bad JSON / wrong method shape
//   401/403 — invalid / rejected receipt (never 5xx for bad receipts)
//   503 — missing ENTITLEMENT_SIGNING_KEY, or receipt-validation
//         dependency down / unconfigured (no partial entitlement)

import { readJsonObject } from "./lib/requestBody.mts";
import {
  issueEntitlementToken,
  loadSigningKey,
  loadTokenTtlSeconds,
} from "./lib/entitlementToken.mts";
import { validateReceipt } from "./lib/receiptValidate.mts";

export const config = { path: "/api/entitlement" };

function json(payload: unknown, status: number): Response {
  return new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

export type EntitlementHandlerDeps = {
  getSigningKey?: () => string | undefined;
  getTtlSeconds?: () => number;
  validateReceipt?: typeof validateReceipt;
  nowMs?: () => number;
  env?: Record<string, string | undefined>;
};

export async function handleEntitlement(
  req: Request,
  deps: EntitlementHandlerDeps = {},
): Promise<Response> {
  if (req.method !== "POST") {
    return json({ error: "Method not allowed. Use POST." }, 405);
  }

  try {
    const body = await readJsonObject(req);
    if (!body.ok) {
      return json({ error: body.error }, 400);
    }

    const env = deps.env ?? process.env;
    const signingKey = (deps.getSigningKey ?? (() => loadSigningKey(env)))();
    if (typeof signingKey !== "string" || signingKey.length === 0) {
      return json(
        { error: "Entitlement issuing is not configured (missing ENTITLEMENT_SIGNING_KEY)." },
        503,
      );
    }

    const validate = deps.validateReceipt ?? validateReceipt;
    const receiptResult = await validate(
      { receipt: body.value.receipt, platform: body.value.platform },
      env,
    );
    if (!receiptResult.ok) {
      return json({ error: receiptResult.error }, receiptResult.status);
    }

    const ttl = (deps.getTtlSeconds ?? (() => loadTokenTtlSeconds(env)))();
    const nowMs = (deps.nowMs ?? (() => Date.now()))();
    const { token, claims } = await issueEntitlementToken(signingKey, receiptResult.subject, {
      ttlSeconds: ttl,
      nowMs,
    });

    return json(
      {
        token,
        expires_at: claims.exp,
        tier: claims.tier,
      },
      200,
    );
  } catch {
    return json({ error: "Internal server error." }, 500);
  }
}

export default async function handler(req: Request): Promise<Response> {
  return handleEntitlement(req);
}
