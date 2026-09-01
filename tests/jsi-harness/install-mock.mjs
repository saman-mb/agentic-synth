#!/usr/bin/env node
// Mock of the React Native native-module install() that attaches a JSI host.
// Real RN: NativeModules.Agsynth.install() → global.__AgsynthNative.
// This file is a Node stand-in; it does not load C++ or an Nx app.

import { pathToFileURL } from 'node:url';

const AGS_OK = 0;

function createMockBinding() {
  return {
    setPatch(_bytes) {
      return AGS_OK;
    },
    setParam(_path, _value) {
      return AGS_OK;
    },
    noteOn(_note, _velocity) {
      return AGS_OK;
    },
    noteOff(_note) {
      return AGS_OK;
    },
    dispose() {
      return AGS_OK;
    },
    recreate(_sampleRate) {
      return AGS_OK;
    },
  };
}

export function install() {
  const binding = createMockBinding();
  globalThis.__AgsynthNative = binding;
  return true;
}

const isMain = Boolean(process.argv[1]) && import.meta.url === pathToFileURL(process.argv[1]).href;

if (isMain) {
  const ok = install();
  const host = globalThis.__AgsynthNative;
  const patchBytes = new ArrayBuffer(828);
  const status = host.setPatch(patchBytes);
  host.setParam('filter.cutoff_hz', 1200);
  host.noteOn(60, 100);
  host.noteOff(60);
  host.recreate(48000);
  host.dispose();
  console.log(
    JSON.stringify({
      installed: ok,
      binding: Object.keys(host).sort(),
      setPatch: status,
      next: 'new JsiSynthEngine(globalThis.__AgsynthNative)',
    }),
  );
}
