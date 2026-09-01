// ── Store receipt validation (#312) ─────────────────────────────────
//
// ENTITLEMENT_RECEIPT_MODE=stub|apple (default stub).
//   stub  — accepts known test receipts for CI / local; never calls Apple.
//   apple — live App Store verifyReceipt path; fails closed when the
//           dependency is unreachable or not configured (no partial grant).
//
// Residual: production Apple verify + Android Play are wired for fail-
// closed behaviour; full live verify against Apple's network is residual
// until mocked in CI / #294 mobile client integration.

export type ReceiptPlatform = "ios" | "android";

export type ReceiptValidateResult =
  | { ok: true; subject: string; platform: ReceiptPlatform }
  | { ok: false; status: 401 | 403 | 503; error: string };

export type ReceiptMode = "stub" | "apple";

const TEST_PREFIX = "test:";

export function loadReceiptMode(
  env: Record<string, string | undefined> = process.env,
): ReceiptMode {
  const raw = env.ENTITLEMENT_RECEIPT_MODE?.trim().toLowerCase();
  if (raw === "apple") return "apple";
  return "stub";
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

  const receiptObj = (payload as { receipt?: { original_transaction_id?: unknown } })
    .receipt;
  const txn =
    typeof receiptObj?.original_transaction_id === "string"
      ? receiptObj.original_transaction_id.trim()
      : "";
  if (txn.length === 0) {
    return { ok: false, status: 403, error: "Receipt was rejected." };
  }

  return { ok: true, subject: `ios:${txn}`, platform: "ios" };
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
  if (mode === "stub") {
    return validateStubReceipt(input.receipt, platform);
  }
  return validateAppleReceipt(input.receipt, platform, env, fetchImpl);
}
