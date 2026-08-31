import type { AgentModulationPlan } from '@agentic-synth/shared-types';
// eslint-disable-next-line @nx/enforce-module-boundaries -- Netlify esbuild cannot resolve tsconfig paths
import { PARAM_RANGES } from '../../../data/src/lib/paramRanges.ts';

export function normaliseModDestination(target: string): string {
  return target.replace(/\[(\d+)\]/g, '.$1');
}

export type ModulationViolationCode =
  | 'invalid_shape'
  | 'out_of_range'
  | 'unknown_target'
  | 'unknown_source';

export interface ModulationViolation {
  path: string;
  code: ModulationViolationCode;
  message: string;
}

export type ModulationVerdict =
  | { ok: true; plan: AgentModulationPlan | undefined }
  | { ok: false; errors: ModulationViolation[]; error: string };

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

function fail(
  errors: ModulationViolation[],
  path: string,
  code: ModulationViolationCode,
  detail: string,
): void {
  errors.push({ path, code, message: `${path}: ${detail}` });
}

function checkAmount(
  errors: ModulationViolation[],
  v: unknown,
  path: string,
): void {
  if (typeof v !== 'number' || !Number.isFinite(v) || v < -1 || v > 1) {
    fail(errors, path, 'out_of_range', 'expected finite number in [-1, 1]');
  }
}

function checkTarget(
  errors: ModulationViolation[],
  v: unknown,
  path: string,
): void {
  if (typeof v !== 'string') {
    fail(errors, path, 'invalid_shape', 'expected string');
    return;
  }
  const dest = normaliseModDestination(v);
  if (!(dest in PARAM_RANGES)) {
    fail(errors, path, 'unknown_target', `${dest} is not a registered destination`);
  }
}

function checkRoute(
  errors: ModulationViolation[],
  route: unknown,
  path: string,
): void {
  if (!isRecord(route)) {
    fail(errors, path, 'invalid_shape', 'expected object');
    return;
  }
  checkTarget(errors, route.target, `${path}.target`);
  checkAmount(errors, route.amount, `${path}.amount`);
}

/**
 * Validate a wire modulation plan (#286 / #301).
 *
 * Collects every violation (not first-error-only). `error` is a joined
 * summary for callers that want a single string; `errors` is the typed list.
 *
 * Cyclic routing: NON-GOAL for the current plan shape. Macros/extras only
 * route modulation *sources* onto PARAM_RANGES *destinations*. Destinations
 * are engine params, not sources, so the wire graph has no source→source
 * edges to close a cycle. If a future schema adds mod-to-mod routing,
 * add a cycle check here and cover it with a unit test.
 */
export function validateModulationPlan(raw: unknown): ModulationVerdict {
  if (raw === undefined || raw === null) {
    return { ok: true, plan: undefined };
  }
  if (!isRecord(raw)) {
    const errors: ModulationViolation[] = [{
      path: 'modulation',
      code: 'invalid_shape',
      message: 'modulation: expected object',
    }];
    return { ok: false, errors, error: errors[0].message };
  }

  const errors: ModulationViolation[] = [];

  if (raw.macros !== undefined) {
    if (!Array.isArray(raw.macros)) {
      fail(errors, 'macros', 'invalid_shape', 'expected array');
    } else {
      for (let i = 0; i < raw.macros.length; i++) {
        const macro = raw.macros[i];
        if (!isRecord(macro)) {
          fail(errors, `macros.${i}`, 'invalid_shape', 'expected object');
          continue;
        }
        if (macro.routes !== undefined) {
          if (!Array.isArray(macro.routes)) {
            fail(errors, `macros.${i}.routes`, 'invalid_shape', 'expected array');
          } else {
            for (let j = 0; j < macro.routes.length; j++) {
              checkRoute(errors, macro.routes[j], `macros.${i}.routes.${j}`);
            }
          }
        }
      }
    }
  }

  if (raw.extras !== undefined) {
    if (!Array.isArray(raw.extras)) {
      fail(errors, 'extras', 'invalid_shape', 'expected array');
    } else {
      for (let i = 0; i < raw.extras.length; i++) {
        const extra = raw.extras[i];
        if (!isRecord(extra)) {
          fail(errors, `extras.${i}`, 'invalid_shape', 'expected object');
          continue;
        }
        if (typeof extra.source !== 'string' || !ALLOWED_SOURCES.has(extra.source)) {
          fail(errors, `extras.${i}.source`, 'unknown_source', 'invalid modulation source');
        }
        checkRoute(errors, extra, `extras.${i}`);
      }
    }
  }

  if (errors.length > 0) {
    return {
      ok: false,
      errors,
      error: errors.map((e) => e.message).join('; '),
    };
  }
  return { ok: true, plan: raw as AgentModulationPlan };
}
