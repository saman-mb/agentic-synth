import type { PatchParams } from '@agentic-synth/shared-types';

import { runMobileGenerateFlow } from './mobileGenerateFlow';
import { nextVariationSeed, perturbPatch } from './perturbVariation';

export type VariationSource = 'api' | 'local';

export interface VariationItem {
  index: number;
  patch: PatchParams;
  seed?: number;
  source: VariationSource;
}

export async function fetchVariation(
  prompt: string,
  index: number,
  fallbackBase: PatchParams,
): Promise<VariationItem> {
  const result = await runMobileGenerateFlow(prompt);
  if (result.ok) {
    return { index, patch: result.patch, source: 'api' };
  }

  const seed = nextVariationSeed(index);
  return {
    index,
    patch: perturbPatch(fallbackBase, seed),
    seed,
    source: 'local',
  };
}
