import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';

import type { VariationItem } from '../services/variationFlow';

export interface SessionScratch {
  prompt: string;
  brief: string;
  basePatch: PatchParams;
  modulation?: AgentModulationPlan;
  variations: VariationItem[];
  selectedVariationIndex: number;
  keepNameDraft: string;
}

export const EMPTY_SCRATCH: SessionScratch = {
  prompt: '',
  brief: '',
  basePatch: {} as PatchParams,
  variations: [],
  selectedVariationIndex: 0,
  keepNameDraft: '',
};

export function defaultKeepName(prompt: string): string {
  const trimmed = prompt.trim();
  if (!trimmed) return 'Untitled';
  return trimmed.length > 32 ? `${trimmed.slice(0, 29)}…` : trimmed;
}
