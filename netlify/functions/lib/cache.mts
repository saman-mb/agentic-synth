// ── Brief LRU cache for the Netlify function (#280) ─────────────────
//
// Caches the enhancer sonic brief keyed by the canonicalized SANITIZED
// prompt, so identical prompts within one isolate skip the enhancer
// call entirely (cache hit = 1 upstream call saved). LRU with a
// 100-entry cap; per-isolate memory only (cold start = empty cache).

export const BRIEF_CACHE_CAPACITY = 100;

/** Canonical cache key: trim, lowercase, collapse whitespace runs. */
export function canonicalKey(prompt: string): string {
  return prompt.trim().toLowerCase().replace(/\s+/g, " ");
}

export class BriefCache {
  private entries: Map<string, string>;
  private readonly capacity: number;

  constructor(capacity: number = BRIEF_CACHE_CAPACITY) {
    this.capacity = capacity;
    this.entries = new Map();
  }

  get(key: string): string | undefined {
    const hit = this.entries.get(key);
    if (hit === undefined) return undefined;
    // Refresh recency: re-insert so iteration order is LRU → MRU.
    this.entries.delete(key);
    this.entries.set(key, hit);
    return hit;
  }

  set(key: string, brief: string): void {
    if (this.entries.has(key)) this.entries.delete(key);
    else if (this.entries.size >= this.capacity) {
      // Evict least-recently-used (first key in insertion order).
      const oldest = this.entries.keys().next();
      if (!oldest.done) this.entries.delete(oldest.value);
    }
    this.entries.set(key, brief);
  }
}
