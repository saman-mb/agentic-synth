/**
 * Node/dev mock for __AgsynthHost — mirrors tests/jsi-harness pattern (#308).
 * Used when NativeModules.Agsynth.install() is unavailable (CI, Expo Go, web).
 */
import type { JsiNativeBinding } from '@agentic-synth/engine-bridge';

export interface MockHostState {
  patchBytes: ArrayBuffer | null;
  notesOn: number[];
  started: boolean;
  disposed: boolean;
  sampleRate: number;
}

export function createMockAgsynthHost(): JsiNativeBinding & { state: MockHostState } {
  const state: MockHostState = {
    patchBytes: null,
    notesOn: [],
    started: false,
    disposed: false,
    sampleRate: 48000,
  };

  const binding: JsiNativeBinding & { state: MockHostState } = {
    state,
    setPatch(bytes: ArrayBuffer): number {
      state.patchBytes = bytes.slice(0);
      return 0;
    },
    setParam(_path: string, _value: number): number {
      return 0;
    },
    noteOn(note: number, _velocity: number): number {
      if (!state.notesOn.includes(note)) state.notesOn.push(note);
      return 0;
    },
    noteOff(note: number): number {
      state.notesOn = state.notesOn.filter((n) => n !== note);
      return 0;
    },
    dispose(): number {
      state.disposed = true;
      state.notesOn = [];
      return 0;
    },
    recreate(sampleRate: number): number {
      state.sampleRate = sampleRate;
      return 0;
    },
    start(): number {
      state.started = true;
      return 0;
    },
  };

  return binding;
}

/** Attach mock to global for JSI-style discovery (dev/test only). */
export function installMockAgsynthHost(): JsiNativeBinding {
  const host = createMockAgsynthHost();
  (globalThis as { __AgsynthHost?: JsiNativeBinding }).__AgsynthHost = host;
  return host;
}
