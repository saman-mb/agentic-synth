// ── Durable / in-memory rate-limit counter store (#309) ─────────────
//
// Keys: rl/v1/{tier}/{subject}/{resource}/{windowType}/{windowId}
// Value: JSON { count: number }. Blobs adapter uses etag CAS so concurrent
// isolates do not clobber each other. MemoryStore is single-process and
// works without @netlify/blobs (unit tests / RATE_LIMIT_STORE=memory).

export type RateLimitResource = "brief" | "generate";
export type WindowType = "minute" | "day";

export interface CounterValue {
  count: number;
}

/** Read result; `etag` undefined means the key does not exist yet. */
export interface CounterSnapshot {
  count: number;
  /** Opaque version for CAS; undefined when the key is absent. */
  etag: string | undefined;
}

export interface CompareAndSetResult {
  /** true when the write landed. */
  ok: boolean;
  etag?: string;
}

/**
 * Low-level counter store. `compareAndSet` must only succeed when the
 * current etag matches `expectedEtag` (undefined = create-if-absent).
 */
export interface RateLimitStore {
  get(key: string): Promise<CounterSnapshot>;
  compareAndSet(
    key: string,
    next: CounterValue,
    expectedEtag: string | undefined,
  ): Promise<CompareAndSetResult>;
}

export function storeKey(
  tier: string,
  subject: string,
  resource: RateLimitResource,
  windowType: WindowType,
  windowId: string,
): string {
  // Slash-separated path; subject may contain colons but not slashes.
  return `rl/v1/${tier}/${subject}/${resource}/${windowType}/${windowId}`;
}

// ── MemoryStore ─────────────────────────────────────────────────────

interface MemoryEntry {
  count: number;
  etag: string;
}

export class MemoryStore implements RateLimitStore {
  private readonly entries = new Map<string, MemoryEntry>();
  private seq = 0;

  async get(key: string): Promise<CounterSnapshot> {
    const hit = this.entries.get(key);
    if (hit === undefined) return { count: 0, etag: undefined };
    return { count: hit.count, etag: hit.etag };
  }

  async compareAndSet(
    key: string,
    next: CounterValue,
    expectedEtag: string | undefined,
  ): Promise<CompareAndSetResult> {
    const cur = this.entries.get(key);
    if (expectedEtag === undefined) {
      if (cur !== undefined) return { ok: false };
    } else if (cur === undefined || cur.etag !== expectedEtag) {
      return { ok: false };
    }
    this.seq += 1;
    const etag = `m${this.seq}`;
    this.entries.set(key, { count: next.count, etag });
    return { ok: true, etag };
  }

  /** Test helper. */
  clear(): void {
    this.entries.clear();
  }
}

// ── Blobs adapter ───────────────────────────────────────────────────

export interface BlobsLike {
  getWithMetadata(
    key: string,
    options: { type: "json"; consistency?: "strong" | "eventual" },
  ): Promise<{ data: unknown; etag?: string } | null>;
  setJSON(
    key: string,
    data: unknown,
    options?: { onlyIfMatch?: string; onlyIfNew?: boolean },
  ): Promise<{ modified: boolean; etag?: string }>;
}

function parseCount(data: unknown): number {
  if (typeof data !== "object" || data === null) return 0;
  const c = (data as { count?: unknown }).count;
  if (typeof c !== "number" || !Number.isFinite(c) || c < 0) return 0;
  return Math.floor(c);
}

export class BlobsRateLimitStore implements RateLimitStore {
  private readonly blobs: BlobsLike;

  constructor(blobs: BlobsLike) {
    this.blobs = blobs;
  }

  async get(key: string): Promise<CounterSnapshot> {
    const hit = await this.blobs.getWithMetadata(key, {
      type: "json",
      consistency: "strong",
    });
    if (hit === null) return { count: 0, etag: undefined };
    return { count: parseCount(hit.data), etag: hit.etag };
  }

  async compareAndSet(
    key: string,
    next: CounterValue,
    expectedEtag: string | undefined,
  ): Promise<CompareAndSetResult> {
    const body = { count: next.count };
    const result =
      expectedEtag === undefined
        ? await this.blobs.setJSON(key, body, { onlyIfNew: true })
        : await this.blobs.setJSON(key, body, { onlyIfMatch: expectedEtag });
    if (!result.modified) return { ok: false };
    return { ok: true, etag: result.etag };
  }
}

/** Build a Blobs-backed store via @netlify/blobs getStore. */
export async function createBlobsRateLimitStore(
  storeName = "rate-limits",
): Promise<RateLimitStore> {
  const { getStore } = await import("@netlify/blobs");
  const blobs = getStore({ name: storeName, consistency: "strong" });
  return new BlobsRateLimitStore(blobs);
}
