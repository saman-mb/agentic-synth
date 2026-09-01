import type { PatchParams } from '@agentic-synth/shared-types';
import { getPatchParam, setPatchParam } from '@agentic-synth/engine-bridge';
import { PARAM_RANGES } from '@agentic-synth/data';

export const MACRO_LABELS = ['Brightness', 'Movement', 'Space', 'Body'] as const;
export const MACRO_DEFAULTS = [0.55, 0.35, 0.3, 0.4] as const;

type Route = {
  param: string;
  weight: number;
  invert?: boolean;
  log?: boolean;
};

/** Fixed v1 bundles — docs/mobile/macros.md */
export const MACRO_BUNDLES: Route[][] = [
  [
    { param: 'filter.cutoff_hz', weight: 1, log: true },
    { param: 'filter.resonance', weight: 0.25, invert: true },
  ],
  [
    { param: 'lfo.0.depth', weight: 1 },
    { param: 'lfo.0.rate_hz', weight: 0.35, log: true },
  ],
  [
    { param: 'reverb.mix', weight: 1 },
    { param: 'reverb.size', weight: 0.35 },
  ],
  [
    { param: 'amp_env.attack_s', weight: 0.6 },
    { param: 'filter.drive', weight: 0.25 },
  ],
];

function clamp(v: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, v));
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

function logLerp(min: number, max: number, t: number): number {
  const lo = Math.log(Math.max(min, 0.0001));
  const hi = Math.log(max);
  return Math.exp(lerp(lo, hi, t));
}

function targetForRoute(baseVal: number, pos: number, route: Route): number {
  const range = PARAM_RANGES[route.param] ?? { min: 0, max: 1 };
  const t = route.invert ? 1 - pos : pos;
  if (route.log) {
    const floor = Math.max(range.min, 0.0001);
    const blend = route.weight * t + (1 - route.weight) * ((baseVal - range.min) / (range.max - range.min));
    return logLerp(floor, range.max, clamp(blend, 0, 1));
  }
  if (route.param === 'amp_env.attack_s') {
    return lerp(range.min, Math.min(range.max, 2), t);
  }
  const span = (range.max - range.min) * route.weight;
  return baseVal + t * span;
}

/** Project all four macro knob positions onto a copy of the base patch. */
export function projectMacroPatch(
  base: PatchParams,
  positions: readonly number[],
): PatchParams {
  const patch = structuredClone(base) as PatchParams;

  positions.forEach((rawPos, macroIndex) => {
    const pos = clamp(rawPos, 0, 1);
    for (const route of MACRO_BUNDLES[macroIndex] ?? []) {
      const range = PARAM_RANGES[route.param] ?? { min: 0, max: 1 };
      const baseVal = getPatchParam(base, route.param) ?? range.min;
      const target = targetForRoute(baseVal, pos, route);
      setPatchParam(patch, route.param, clamp(target, range.min, range.max));
    }
  });

  return patch;
}

export function clampMacroPositions(positions: number[]): number[] {
  return positions.map((p, i) => clamp(Number.isFinite(p) ? p : MACRO_DEFAULTS[i]!, 0, 1));
}

/** Serializable macro positions for Keep / patch JSON sidecar. */
export function macroPositionsForKeep(positions: readonly number[]): number[] {
  return clampMacroPositions([...positions]);
}
