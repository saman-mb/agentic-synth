// ── Store receipt validation (#312) ─────────────────────────────────
//
// ENTITLEMENT_RECEIPT_MODE=stub|apple (default: unconfigured / fail-closed).
//   stub  — only when ENTITLEMENT_RECEIPT_MODE=stub AND
//           ENTITLEMENT_ALLOW_STUB_RECEIPTS=1 (CI/local; never prod default).
//   apple — live App Store verifyReceipt; requires APPLE_SHARED_SECRET +
//           APPLE_BUNDLE_ID; optional APPLE_PRODUCT_ID for SKU scoping.
//
// Residual: production Apple verify + Android Play are wired for fail-
// closed behaviour; full live verify against Apple's network is residual
// until mocked in CI / #294 mobile client integration.

export type ReceiptPlatform = "ios" | "android";

export type ReceiptValidateResult =
  | { ok: true; subject: string; platform: ReceiptPlatform }
  | { ok: false; status: 401 | 403 | 503; error: string };

export type ReceiptMode = "stub" | "apple" | "unconfigured";

const TEST_PREFIX = "test:";

/** Stub receipts are opt-in — never the silent default when a signing key exists. */
export function stubReceiptsEnabled(
  env: Record<string, string | undefined> = process.env,
): boolean {
  const raw = env.ENTITLEMENT_RECEIPT_MODE?.trim().toLowerCase();
  return raw === "stub" && env.ENTITLEMENT_ALLOW_STUB_RECEIPTS?.trim() === "1";
}

export function loadReceiptMode(
  env: Record<string, string | undefined> = process.env,
): ReceiptMode {
  const raw = env.ENTITLEMENT_RECEIPT_MODE?.trim().toLowerCase();
  if (raw === "apple") return "apple";
  if (stubReceiptsEnabled(env)) return "stub";
  return "unconfigured";
}

function parsePlatform(raw: unknown): ReceiptPlatform | null {
  if (raw === undefined || raw === null) return "ios";
  if (raw === "ios" || raw === "android") return raw;
  return null;
}

/** Stub: `test:<subject>` receipts only. Invalid → 401/403, never 5xx. */
function validateStubReceipt(
  receipt: string,
  platform: ReceiptPlatform,
): ReceiptValidateResult {
  const trimmed = receipt.trim();
  if (trimmed.length === 0) {
    return { ok: false, status: 401, error: "Receipt is required." };
  }
  if (!trimmed.startsWith(TEST_PREFIX)) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }
  const subjectPart = trimmed.slice(TEST_PREFIX.length).trim();
  if (subjectPart.length === 0) {
    return { ok: false, status: 401, error: "Receipt is invalid." };
  }
  // Stable subject for rate-limit keys (platform-scoped).
  return {
    ok: true,
    subject: `${platform}:${subjectPart}`,
    platform,
  };
}

/**
 * Apple / Play live path. Without network credentials, fail closed with
 * a clear 503 — never invent a subject. When `APPLE_SHARED_SECRET` is set,
 * attempts Apple's verifyReceipt endpoint and maps failures to 401/403/503.
 */
async function validateAppleReceipt(
  receipt: string,
  platform: ReceiptPlatform,
  env: Record<string, string | undefined>,
  fetchImpl: typeof fetch = fetch,
): Promise<ReceiptValidateResult> {
  const trimmed = receipt.trim();
  if (trimmed.length === 0) {
    return { ok: false, status: 401, error: "Receipt is required." };
  }

  if (platform === "android") {
    // Play Billing verify not implemented yet — fail closed, no partial grant.
    return {
      ok: false,
      status: 503,
      error: "Play Store receipt validation is unavailable.",
    };
  }

  const sharedSecret = env.APPLE_SHARED_SECRET?.trim();
  if (sharedSecret === undefined || sharedSecret.length === 0) {
    return {
      ok: false,
      status: 503,
      error: "App Store receipt validation is not configured.",
    };
  }

  const body = JSON.stringify({
    "receipt-data": trimmed,
    password: sharedSecret,
    "exclude-old-transactions": true,
  });

  let response: Response;
  try {
    response = await fetchImpl("https://buy.itunes.apple.com/verifyReceipt", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body,
    });
  } catch {
    return {
      ok: false,
      status: 503,
      error: "App Store receipt validation is unreachable.",
    };
  }

  if (!response.ok) {
    return {
      ok: false,
      status: 503,
      error: "App Store receipt validation is unreachable.",
    };
  }

  let payload: unknown;
  try {
    payload = await response.json();
  } catch {
    return {
      ok: false,
      status: 503,
      error: "App Store receipt validation returned an unreadable response.",
    };
  }

  if (typeof payload !== "object" || payload === null) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }

  const status = (payload as { status?: unknown }).status;
  // Apple status 21007 = sandbox receipt sent to production → retry sandbox.
  if (status === 21007) {
    try {
      response = await fetchImpl("https://sandbox.itunes.apple.com/verifyReceipt", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body,
      });
      payload = await response.json();
    } catch {
      return {
        ok: false,
        status: 503,
        error: "App Store receipt validation is unreachable.",
      };
    }
  }

  const finalStatus = (payload as { status?: unknown }).status;
  if (finalStatus !== 0) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }

  return validateApplePayload(payload, env);
}

type AppleInAppRow = {
  original_transaction_id?: unknown;
  product_id?: unknown;
  expires_date_ms?: unknown;
  cancellation_date_ms?: unknown;
};

function validateApplePayload(
  payload: unknown,
  env: Record<string, string | undefined>,
): ReceiptValidateResult {
  if (typeof payload !== "object" || payload === null) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }

  const expectedBundle = env.APPLE_BUNDLE_ID?.trim();
  if (expectedBundle === undefined || expectedBundle.length === 0) {
    return {
      ok: false,
      status: 503,
      error: "App Store receipt validation is not configured (missing APPLE_BUNDLE_ID).",
    };
  }

  const receiptObj = (payload as { receipt?: { bundle_id?: unknown; in_app?: unknown } })
    .receipt;
  const bundleId =
    typeof receiptObj?.bundle_id === "string" ? receiptObj.bundle_id.trim() : "";
  if (bundleId !== expectedBundle) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }

  const expectedProduct = env.APPLE_PRODUCT_ID?.trim();
  const nowMs = Date.now();

  const rows: AppleInAppRow[] = [];
  const latest = (payload as { latest_receipt_info?: unknown }).latest_receipt_info;
  if (Array.isArray(latest)) rows.push(...(latest as AppleInAppRow[]));
  else if (Array.isArray(receiptObj?.in_app)) rows.push(...(receiptObj.in_app as AppleInAppRow[]));

  let bestTxn = "";
  let bestExp = -1;

  for (const row of rows) {
    if (row.cancellation_date_ms !== undefined && row.cancellation_date_ms !== null) {
      continue;
    }
    const productId = typeof row.product_id === "string" ? row.product_id.trim() : "";
    if (expectedProduct !== undefined && expectedProduct.length > 0 && productId !== expectedProduct) {
      continue;
    }
    const txn =
      typeof row.original_transaction_id === "string"
        ? row.original_transaction_id.trim()
        : "";
    if (txn.length === 0) continue;

    const expRaw = row.expires_date_ms;
    const expMs =
      typeof expRaw === "string" || typeof expRaw === "number"
        ? Number(expRaw)
        : Number.NaN;
    // Non-subscription / missing expiry: accept if no expiry field (one-time).
    if (Number.isFinite(expMs) && expMs <= nowMs) continue;

    const sortKey = Number.isFinite(expMs) ? expMs : Number.MAX_SAFE_INTEGER;
    if (sortKey > bestExp) {
      bestExp = sortKey;
      bestTxn = txn;
    }
  }

  if (bestTxn.length === 0) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }

  return { ok: true, subject: `ios:${bestTxn}`, platform: "ios" };
}

export type ValidateReceiptInput = {
  receipt: unknown;
  platform?: unknown;
};

/**
 * Validate a store receipt. Never grants a subject on dependency failure.
 */
export async function validateReceipt(
  input: ValidateReceiptInput,
  env: Record<string, string | undefined> = process.env,
  fetchImpl: typeof fetch = fetch,
): Promise<ReceiptValidateResult> {
  if (typeof input.receipt !== "string") {
    return { ok: false, status: 401, error: "Receipt is required." };
  }
  const platform = parsePlatform(input.platform);
  if (platform === null) {
    return { ok: false, status: 401, error: "platform must be ios or android." };
  }

  const mode = loadReceiptMode(env);
  if (mode === "unconfigured") {
    return {
      ok: false,
      status: 503,
      error: "Store receipt validation is not configured.",
    };
  }
  if (mode === "stub") {
    return validateStubReceipt(input.receipt, platform);
  }
  return validateAppleReceipt(input.receipt, platform, env, fetchImpl);
}
