import type { AgentModulationPlan } from '@agentic-synth/shared-types';
// eslint-disable-next-line @nx/enforce-module-boundaries -- Netlify esbuild cannot resolve tsconfig paths
import { PARAM_RANGES } from '../../../data/src/lib/paramRanges.ts';

export function normaliseModDestination(target: string): string {
  return target.replace(/\[(\d+)\]/g, '.$1');
}

export type ModulationVerdict =
  | { ok: true; plan: AgentModulationPlan | undefined }
  | { ok: false; error: string };

const ALLOWED_SOURCES: ReadonlySet<string> = new Set([
  'lfo1',
  'lfo2',
  'env1',
  'env2',
  'macro1',
  'macro2',
  'macro3',
  'macro4',
  'velocity',
  'keytrack',
  'LFO1',
  'LFO2',
  'FilterEnv',
  'AmpEnv',
  'Velocity',
  'KeyTrack',
]);

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function checkAmount(v: unknown, path: string): string | undefined {
  if (typeof v !== 'number' || !Number.isFinite(v) || v < -1 || v > 1) {
    return `${path}: expected finite number in [-1, 1]`;
  }
  return undefined;
}

function checkTarget(v: unknown, path: string): string | undefined {
  if (typeof v !== 'string') {
    return `${path}: expected string`;
  }
  const dest = normaliseModDestination(v);
  if (!(dest in PARAM_RANGES)) {
    return `${path}: ${dest} is not a registered destination`;
  }
  return undefined;
}

function checkRoute(route: unknown, path: string): string | undefined {
  if (!isRecord(route)) {
    return `${path}: expected object`;
  }
  const targetErr = checkTarget(route.target, `${path}.target`);
  if (targetErr) return targetErr;
  return checkAmount(route.amount, `${path}.amount`);
}

export function validateModulationPlan(raw: unknown): ModulationVerdict {
  if (raw === undefined || raw === null) {
    return { ok: true, plan: undefined };
  }
  if (!isRecord(raw)) {
    return { ok: false, error: 'modulation: expected object' };
  }

  if (raw.macros !== undefined) {
    if (!Array.isArray(raw.macros)) {
      return { ok: false, error: 'macros: expected array' };
    }
    for (let i = 0; i < raw.macros.length; i++) {
      const macro = raw.macros[i];
      if (!isRecord(macro)) {
        return { ok: false, error: `macros.${i}: expected object` };
      }
      if (macro.routes !== undefined) {
        if (!Array.isArray(macro.routes)) {
          return { ok: false, error: `macros.${i}.routes: expected array` };
        }
        for (let j = 0; j < macro.routes.length; j++) {
          const err = checkRoute(macro.routes[j], `macros.${i}.routes.${j}`);
          if (err) return { ok: false, error: err };
        }
      }
    }
  }

  if (raw.extras !== undefined) {
    if (!Array.isArray(raw.extras)) {
      return { ok: false, error: 'extras: expected array' };
    }
    for (let i = 0; i < raw.extras.length; i++) {
      const extra = raw.extras[i];
      if (!isRecord(extra)) {
        return { ok: false, error: `extras.${i}: expected object` };
      }
      if (typeof extra.source !== 'string' || !ALLOWED_SOURCES.has(extra.source)) {
        return { ok: false, error: `extras.${i}.source: invalid modulation source` };
      }
      const err = checkRoute(extra, `extras.${i}`);
      if (err) return { ok: false, error: err };
    }
  }

  return { ok: true, plan: raw as AgentModulationPlan };
}
