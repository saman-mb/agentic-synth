import type { PatchParams } from '@agentic-synth/shared-types';
import { getPatchParam, setPatchParam } from '@agentic-synth/engine-bridge';

/** Deterministic local variation when API rate-limits (#319). */
export function perturbPatch(base: PatchParams, seed: number): PatchParams {
  const next = structuredClone(base) as PatchParams;
  const rnd = mulberry32(seed);

  const nudge = (param: string, span: number) => {
    const current = getPatchParam(next, param);
    if (current === undefined) return;
    const delta = (rnd() - 0.5) * span;
    setPatchParam(next, param, current + delta);
  };

  nudge('filter.cutoff_hz', 800);
  nudge('filter.resonance', 0.12);
  nudge('lfo.0.depth', 0.15);
  nudge('lfo.0.rate_hz', 2);
  nudge('reverb.mix', 0.12);
  nudge('reverb.size', 0.1);
  nudge('amp_env.attack_s', 0.08);
  nudge('filter.drive', 0.08);

  return next;
}

function mulberry32(a: number): () => number {
  return () => {
    let t = (a += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function nextVariationSeed(index: number): number {
  return (Date.now() >>> 0) ^ (index + 1) * 0x9e3779b9;
}
