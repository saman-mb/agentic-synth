/** Minimal session scratch for keep-flow. Chat history is the primary state now. */
export interface SessionScratch {
  keepNameDraft: string;
}

export const EMPTY_SCRATCH: SessionScratch = {
  keepNameDraft: '',
};

export function defaultKeepName(prompt: string): string {
  const trimmed = prompt.trim();
  if (!trimmed) return 'Untitled';
  return trimmed.length > 32 ? `${trimmed.slice(0, 29)}…` : trimmed;
}
