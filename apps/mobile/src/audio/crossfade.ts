import type { PatchParams } from '@agentic-synth/shared-types';
import { getPatchParam, setPatchParam } from '@agentic-synth/engine-bridge';
import { PARAM_RANGES } from '@agentic-synth/data';

const NUMERIC_PARAMS = Object.keys(PARAM_RANGES);

function clamp(v: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, v));
}

/** Linear blend between two patches for crossfade (#319). */
export function lerpPatch(from: PatchParams, to: PatchParams, t: number): PatchParams {
  const out = structuredClone(from) as PatchParams;
  for (const param of NUMERIC_PARAMS) {
    const a = getPatchParam(from, param);
    const b = getPatchParam(to, param);
    if (a === undefined || b === undefined) continue;
    const range = PARAM_RANGES[param]!;
    setPatchParam(out, param, clamp(a + (b - a) * t, range.min, range.max));
  }
  return out;
}

export const CROSSFADE_MS = 250;

const scheduleFrame =
  typeof requestAnimationFrame === 'function'
    ? requestAnimationFrame
    : (cb: FrameRequestCallback) => setTimeout(() => cb(performance.now()), 0);

export async function crossfadePatches(
  apply: (patch: PatchParams) => void,
  from: PatchParams,
  to: PatchParams,
  ms = CROSSFADE_MS,
): Promise<void> {
  const start = performance.now();
  return new Promise((resolve) => {
    const step = (now: number) => {
      const t = Math.min(1, (now - start) / ms);
      apply(lerpPatch(from, to, t));
      if (t < 1) {
        scheduleFrame(step);
      } else {
        apply(to);
        resolve();
      }
    };
    scheduleFrame(step);
  });
}
