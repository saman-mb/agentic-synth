import { validatePrompt, type PromptValidation } from './validatePrompt.ts';

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/**
 * Request-shape gate for POST /api/brief (#311).
 * Wire error strings match validatePrompt — keep stable for clients.
 */
export function validateBriefRequest(raw: unknown): PromptValidation {
  if (!isRecord(raw)) {
    return { ok: false, error: 'Request body must be a JSON object.' };
  }
  return validatePrompt(raw['prompt']);
}
