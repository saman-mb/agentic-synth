// ── Shared rate-limit gate for Netlify functions (#309) ─────────────
//
// Lazy singleton: config + entitlement verifier + store. Both brief.mts
// and generate.mts call gateRateLimit before any Gemini work.

import { StubEntitlementVerifier, type EntitlementVerifier } from "./entitlement.mts";
import { resolveIdentity } from "./identity.mts";
import {
  enforceRateLimit,
  rateLimitedBody,
  rateLimitUnavailableBody,
} from "./rateLimit.mts";
import {
  limitsForTier,
  loadRateLimitConfig,
  type RateLimitConfig,
} from "./rateLimitConfig.mts";
import {
  createBlobsRateLimitStore,
  MemoryStore,
  type RateLimitResource,
  type RateLimitStore,
} from "./rateLimitStore.mts";
import { noteRateLimited } from "./quotaGate.mts";

export interface RateLimitRuntime {
  config: RateLimitConfig;
  verifier: EntitlementVerifier;
  store: RateLimitStore;
}

let runtimePromise: Promise<RateLimitRuntime> | undefined;

/** Test seam: replace or clear the lazy singleton. */
export function resetRateLimitRuntime(): void {
  runtimePromise = undefined;
}

export function setRateLimitRuntime(runtime: RateLimitRuntime): void {
  runtimePromise = Promise.resolve(runtime);
}

async function buildRuntime(): Promise<RateLimitRuntime> {
  const config = loadRateLimitConfig();
  const verifier = new StubEntitlementVerifier(config);
  let store: RateLimitStore;
  if (config.store === "memory") {
    store = new MemoryStore();
  } else {
    try {
      store = await createBlobsRateLimitStore();
    } catch {
      // getStore throws outside Netlify / without credentials. Leave a
      // MemoryStore only when FAIL_MODE=open would still need a store for
      // the allow path; for closed we still need *a* store object so
      // enforce can attempt and surface unavailable on subsequent errors.
      // Prefer failing closed on first enforce by wrapping a throwing store.
      store = {
        async get() {
          throw new Error("rate limit blobs store unavailable");
        },
        async compareAndSet() {
          throw new Error("rate limit blobs store unavailable");
        },
      };
    }
  }
  return { config, verifier, store };
}

export function getRateLimitRuntime(): Promise<RateLimitRuntime> {
  if (runtimePromise === undefined) {
    runtimePromise = buildRuntime();
  }
  return runtimePromise;
}

function json(payload: unknown, status: number, extraHeaders?: HeadersInit): Response {
  const headers = new Headers(extraHeaders);
  headers.set("Content-Type", "application/json");
  return new Response(JSON.stringify(payload), { status, headers });
}

/**
 * Run identity + rate limit. Returns a Response to short-circuit the
 * handler, or null when the request may proceed to Gemini.
 */
export async function gateRateLimit(
  req: Request,
  resource: RateLimitResource,
  now: number = Date.now(),
): Promise<Response | null> {
  const { config, verifier, store } = await getRateLimitRuntime();

  const identityResult = await resolveIdentity(req, verifier);
  if (!identityResult.ok) {
    return json({ error: identityResult.error }, identityResult.status);
  }

  const limits = limitsForTier(config, identityResult.identity.tier);
  const result = await enforceRateLimit({
    store,
    identity: identityResult.identity,
    resource,
    limits,
    failMode: config.failMode,
    now,
  });

  if (result.ok) return null;

  if (result.kind === "unavailable") {
    return json(rateLimitUnavailableBody(result), 503);
  }

  // Abuse signal (#313): identity hammering 429s.
  void noteRateLimited(identityResult.identity.subject, now);

  return json(rateLimitedBody(result), 429, {
    "Retry-After": String(result.retryAfterSeconds),
  });
}
