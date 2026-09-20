import { JsiSynthEngine, type SynthEngine } from '@agentic-synth/engine-bridge';
import type { PatchParams } from '@agentic-synth/shared-types';
import { installMockAgsynthHost } from './mockAgsynthHost';

export type EngineBackend = 'native' | 'mock';

export interface MobileEngineResult {
  engine: SynthEngine;
  backend: EngineBackend;
}

declare global {
  var __AgsynthHost: import('@agentic-synth/engine-bridge').JsiNativeBinding | undefined;
}

interface AgsynthNativeModule {
  install?: () => import('@agentic-synth/engine-bridge').JsiNativeBinding;
}

function tryNativeBinding(): import('@agentic-synth/engine-bridge').JsiNativeBinding | null {
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const { installNativeHost } = require('../../modules/agsynth/src/index') as {
      installNativeHost?: () => import('@agentic-synth/engine-bridge').JsiNativeBinding | null;
    };
    if (typeof installNativeHost === 'function') {
      const fromExpo = installNativeHost();
      if (fromExpo) return fromExpo;
    }
  } catch {
    // Expo runtime not available (Node tests, web)
  }

  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const { NativeModules } = require('react-native') as {
      NativeModules?: { Agsynth?: AgsynthNativeModule };
    };
    const mod = NativeModules?.Agsynth;
    if (mod && typeof mod.install === 'function') {
      const binding = mod.install();
      if (binding) return binding;
    }
  } catch {
    // react-native not available (Node tests, web)
  }

  if (globalThis.__AgsynthHost) {
    return globalThis.__AgsynthHost;
  }

  return null;
}

export function shouldUseMockEngine(forceMock?: boolean): boolean {
  if (forceMock === true) return true;
  if (process.env.AGSYNTH_FORCE_MOCK === '1') return true;
  if (process.env.NODE_ENV === 'test') return true;
  return tryNativeBinding() === null;
}

export function createMobileEngine(opts?: { forceMock?: boolean }): MobileEngineResult {
  const native = tryNativeBinding();
  const useMock =
    opts?.forceMock === true ||
    process.env.AGSYNTH_FORCE_MOCK === '1' ||
    process.env.NODE_ENV === 'test' ||
    native === null;

  const binding = useMock ? installMockAgsynthHost() : native;
  if (!binding) {
    throw new Error('No JSI binding available');
  }

  return {
    engine: new JsiSynthEngine(binding),
    backend: useMock ? 'mock' : 'native',
  };
}

export async function bootDemoPatch(
  engine: SynthEngine,
  patch: PatchParams,
  note = 60,
  velocity = 100,
): Promise<void> {
  engine.setPatch(patch);
  await engine.ensureStarted();
  engine.noteOn(note, velocity);
}
