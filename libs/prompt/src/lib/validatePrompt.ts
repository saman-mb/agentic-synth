/** Shared prompt length cap for /api/brief and /api/generate. */
export const MAX_PROMPT_LENGTH = 2000;

export type PromptValidation =
  | { ok: true; prompt: string }
  | { ok: false; error: string };

/**
 * Empty / over-length gate used by Netlify handlers and clients (#300).
 * Rejection messages are the wire SSOT — keep them stable for regression.
 */
export function validatePrompt(prompt: unknown): PromptValidation {
  if (typeof prompt !== 'string' || prompt.trim().length === 0) {
    return { ok: false, error: 'prompt must be a non-empty string.' };
  }
  if (prompt.length > MAX_PROMPT_LENGTH) {
    return {
      ok: false,
      error: `prompt must be at most ${MAX_PROMPT_LENGTH} characters.`,
    };
  }
  return { ok: true, prompt };
}
