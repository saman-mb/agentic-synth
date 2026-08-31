import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
// eslint-disable-next-line @nx/enforce-module-boundaries -- Netlify esbuild cannot resolve tsconfig paths; keep leaf .ts import
import { validatePatch } from '../../../codec/src/lib/patchCodec.ts';
import { validateModulationPlan } from './validateModulationPlan.ts';

export type GeneratePayloadVerdict =
  | { ok: true; patch: PatchParams; modulation: AgentModulationPlan | undefined }
  | { ok: false; error: string };

/**
 * Defence-in-depth gate for generateFlow: validate patch + modulation
 * before WebAudio ingestion (#301 / #286).
 */
export function validateGeneratePayload(
  patch: unknown,
  modulation: unknown,
): GeneratePayloadVerdict {
  const validation = validatePatch(patch);
  if (!validation.ok) {
    return {
      ok: false,
      error: `The patch service returned an invalid patch (${validation.error}).`,
    };
  }
  const modVerdict = validateModulationPlan(modulation);
  if (!modVerdict.ok) {
    return {
      ok: false,
      error: `The patch service returned an invalid modulation (${modVerdict.error}).`,
    };
  }
  return {
    ok: true,
    patch: patch as PatchParams,
    modulation: modVerdict.plan,
  };
}
