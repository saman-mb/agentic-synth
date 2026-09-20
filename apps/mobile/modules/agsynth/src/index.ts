/**
 * Expo module stub — wires NativeModules.Agsynth.install() → __AgsynthHost (JSI).
 * Real native build links src/jsi/ AudioStream + ags_engine on the RT thread (#316 scaffold).
 */
import { requireNativeModule } from 'expo-modules-core';
import type { JsiNativeBinding } from '@agentic-synth/engine-bridge';

export interface AgsynthModuleType {
  install(): JsiNativeBinding;
}

let cached: AgsynthModuleType | null = null;

export function getAgsynthModule(): AgsynthModuleType | null {
  if (cached) return cached;
  try {
    cached = requireNativeModule<AgsynthModuleType>('Agsynth');
    return cached;
  } catch {
    return null;
  }
}

export function installNativeHost(): JsiNativeBinding | null {
  const mod = getAgsynthModule();
  if (!mod || typeof mod.install !== 'function') return null;
  return mod.install();
}
