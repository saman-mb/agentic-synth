import { useCallback, useEffect, useState } from 'react';

// ── UI Audio Settings (Phase 10 §17) ────────────────────────────────
//
// Two opt-in confirmation sounds. Both default OFF — TIMBRE is mostly
// silent. Persisted to localStorage so user preference survives reload.

const VOICE_KEY = 'timbre:ui-audio-voice';
const PATCH_KEY = 'timbre:ui-audio-patch';

export interface UiAudioSettings {
  voicePip: boolean;
  patchThunk: boolean;
  setVoicePip: (v: boolean) => void;
  setPatchThunk: (v: boolean) => void;
}

function readBool(key: string): boolean {
  if (typeof window === 'undefined') return false;
  try {
    return window.localStorage.getItem(key) === '1';
  } catch {
    return false;
  }
}

function writeBool(key: string, v: boolean): void {
  if (typeof window === 'undefined') return;
  try {
    window.localStorage.setItem(key, v ? '1' : '0');
  } catch {
    // private-mode browsers may throw — ignore.
  }
}

export function useUiAudioSettings(): UiAudioSettings {
  const [voicePip, setVoicePipState] = useState<boolean>(() => readBool(VOICE_KEY));
  const [patchThunk, setPatchThunkState] = useState<boolean>(() => readBool(PATCH_KEY));

  useEffect(() => { writeBool(VOICE_KEY, voicePip); }, [voicePip]);
  useEffect(() => { writeBool(PATCH_KEY, patchThunk); }, [patchThunk]);

  const setVoicePip = useCallback((v: boolean) => setVoicePipState(v), []);
  const setPatchThunk = useCallback((v: boolean) => setPatchThunkState(v), []);

  return { voicePip, patchThunk, setVoicePip, setPatchThunk };
}
