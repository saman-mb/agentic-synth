import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
import { sanitizePrompt, validatePrompt, parseBriefResponse } from '@agentic-synth/prompt';
import { validateGeneratePayload } from '@agentic-synth/modval';

import { briefUrl, generateUrl } from '../config/api';
import { friendlyError } from './friendlyError';

export type GenerateFlowResult =
  | {
      ok: true;
      patch: PatchParams;
      brief: string;
      modulation?: AgentModulationPlan;
    }
  | { ok: false; message: string; retryable: boolean };

interface GenerateApiResponse {
  brief?: unknown;
  patch?: unknown;
  modulation?: unknown;
  error?: unknown;
}

function asString(v: unknown): string | undefined {
  return typeof v === 'string' && v.length > 0 ? v : undefined;
}

async function postJson(url: string, body: unknown): Promise<Response> {
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

/** Brief → generate pipeline for mobile (#317). Offline-safe: throws only on network. */
export async function runMobileGenerateFlow(prompt: string): Promise<GenerateFlowResult> {
  const promptGate = validatePrompt(prompt);
  if (!promptGate.ok) {
    const message = promptGate.error.includes('non-empty')
      ? "Didn't catch that — type or record a sound description."
      : promptGate.error;
    return { ok: false, message, retryable: true };
  }

  const cleaned = sanitizePrompt(promptGate.prompt);

  let briefRes: Response;
  try {
    briefRes = await postJson(briefUrl(), { prompt: cleaned });
  } catch {
    return {
      ok: false,
      message: "You're offline — connect to generate.",
      retryable: true,
    };
  }

  let briefData: unknown = {};
  try {
    briefData = JSON.parse(await briefRes.text()) as unknown;
  } catch {
    // status check below
  }

  if (!briefRes.ok) {
    const parsed = parseBriefResponse(briefData);
    const detail = !parsed.ok ? parsed.error : undefined;
    return {
      ok: false,
      message: friendlyError(briefRes.status, detail),
      retryable: briefRes.status === 429 || briefRes.status >= 500,
    };
  }

  const briefParsed = parseBriefResponse(briefData);
  if (!briefParsed.ok) {
    return { ok: false, message: briefParsed.error, retryable: true };
  }
  const brief = briefParsed.brief;

  let genRes: Response;
  try {
    genRes = await postJson(generateUrl(), { prompt: cleaned, brief });
  } catch {
    return {
      ok: false,
      message: "You're offline — connect to generate.",
      retryable: true,
    };
  }

  let data: GenerateApiResponse = {};
  try {
    data = JSON.parse(await genRes.text()) as GenerateApiResponse;
  } catch {
    // fall through
  }

  if (!genRes.ok) {
    return {
      ok: false,
      message: friendlyError(genRes.status, asString(data.error)),
      retryable: genRes.status === 429 || genRes.status >= 500,
    };
  }

  const serverError = asString(data.error);
  if (serverError) {
    return { ok: false, message: serverError, retryable: true };
  }

  const gated = validateGeneratePayload(data.patch, data.modulation);
  if (!gated.ok) {
    return { ok: false, message: gated.error, retryable: true };
  }

  return {
    ok: true,
    patch: gated.patch,
    brief,
    modulation: gated.modulation,
  };
}
