import type { PatchParams } from '@agentic-synth/shared-types';
import { validatePatch } from '@agentic-synth/codec';
import { packPatchParams } from '@agentic-synth/engine-bridge';

export interface KeptPreset {
  id: string;
  name: string;
  prompt: string;
  patch: PatchParams;
  macros: number[];
  variation: { index: number; seed?: number };
  createdAt: string;
}

export interface PresetStorage {
  getItem(key: string): Promise<string | null>;
  setItem(key: string, value: string): Promise<void>;
}

const STORAGE_KEY = '@tambra/kept-presets/v1';

export function createMemoryStorage(): PresetStorage & { data: Map<string, string> } {
  const data = new Map<string, string>();
  return {
    data,
    getItem: async (k) => data.get(k) ?? null,
    setItem: async (k, v) => {
      data.set(k, v);
    },
  };
}

function newId(): string {
  return `preset_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
}

export async function loadPresets(storage: PresetStorage): Promise<KeptPreset[]> {
  const raw = await storage.getItem(STORAGE_KEY);
  if (!raw) return [];
  try {
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return [];
    const out: KeptPreset[] = [];
    for (const row of parsed) {
      const preset = sanitizePreset(row);
      if (preset) out.push(preset);
    }
    return out.sort((a, b) => b.createdAt.localeCompare(a.createdAt));
  } catch {
    return [];
  }
}

function sanitizePreset(row: unknown): KeptPreset | null {
  if (!row || typeof row !== 'object') return null;
  const r = row as Record<string, unknown>;
  if (typeof r.id !== 'string' || typeof r.name !== 'string') return null;
  if (typeof r.prompt !== 'string' || typeof r.createdAt !== 'string') return null;
  const gated = validatePatch(r.patch);
  if (!gated.ok) return null;
  const patch = r.patch as PatchParams;
  const macros = Array.isArray(r.macros)
    ? r.macros.filter((m): m is number => typeof m === 'number').slice(0, 4)
    : [];
  while (macros.length < 4) macros.push(0.5);
  const variation =
    r.variation && typeof r.variation === 'object'
      ? {
          index: Number((r.variation as { index?: unknown }).index) || 0,
          seed:
            typeof (r.variation as { seed?: unknown }).seed === 'number'
              ? (r.variation as { seed: number }).seed
              : undefined,
        }
      : { index: 0 };
  return {
    id: r.id,
    name: r.name,
    prompt: r.prompt,
    patch,
    macros,
    variation,
    createdAt: r.createdAt,
  };
}

export async function savePreset(
  storage: PresetStorage,
  input: Omit<KeptPreset, 'id' | 'createdAt'>,
): Promise<KeptPreset> {
  const gated = validatePatch(input.patch);
  if (!gated.ok) throw new Error(gated.error);
  const preset: KeptPreset = {
    id: newId(),
    createdAt: new Date().toISOString(),
    ...input,
    patch: input.patch,
    macros: input.macros.slice(0, 4),
  };
  const existing = await loadPresets(storage);
  await storage.setItem(STORAGE_KEY, JSON.stringify([preset, ...existing]));
  return preset;
}

/** Codec round-trip: JSON → validate → pack produces stable bytes. */
export function presetPatchBytes(preset: KeptPreset): Uint8Array {
  const gated = validatePatch(preset.patch);
  if (!gated.ok) throw new Error(gated.error);
  return new Uint8Array(packPatchParams(preset.patch as PatchParams));
}

export async function getAsyncStorage(): Promise<PresetStorage | null> {
  try {
    const mod = await import('@react-native-async-storage/async-storage');
    return mod.default;
  } catch {
    return null;
  }
}
