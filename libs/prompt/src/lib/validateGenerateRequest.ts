import { validatePrompt } from './validatePrompt.ts';

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

export type GenerateRequestValidation =
  | { ok: true; prompt: string; patchId: number; brief: string | undefined }
  | { ok: false; error: string };

/**
 * Request-shape gate for POST /api/generate (#311).
 * Brief is optional; non-string brief values are ignored (same as prior handler).
 */
export function validateGenerateRequest(raw: unknown): GenerateRequestValidation {
  if (!isRecord(raw)) {
    return { ok: false, error: 'Request body must be a JSON object.' };
  }
  const promptGate = validatePrompt(raw['prompt']);
  if (!promptGate.ok) {
    return promptGate;
  }

  let patchId = 0;
  const rawPatchId = raw['patch_id'];
  if (rawPatchId !== undefined) {
    if (typeof rawPatchId !== 'number' || !Number.isInteger(rawPatchId) || rawPatchId < 0) {
      return { ok: false, error: 'patch_id must be an integer >= 0.' };
    }
    patchId = rawPatchId;
  }

  let brief: string | undefined;
  const rawBrief = raw['brief'];
  if (typeof rawBrief === 'string' && rawBrief.trim().length > 0) {
    brief = rawBrief;
  }

  return { ok: true, prompt: promptGate.prompt, patchId, brief };
}
