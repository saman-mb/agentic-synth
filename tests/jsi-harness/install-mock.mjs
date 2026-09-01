#!/usr/bin/env node
// Mock of the React Native native-module install() that attaches a JSI host.
// Real RN: NativeModules.Agsynth.install() returns true and attaches
// global.__AgsynthHost. It does not return the binding.
// This file is a Node stand-in; it does not load C++ or an Nx app.

import { pathToFileURL } from 'node:url';

const AGS_OK = 0;

function createMockHost() {
  return {
    create(_sampleRate, _maxBlock) {
      return AGS_OK;
    },
    destroy() {
      return AGS_OK;
    },
    setPatch(_bytes) {
      return AGS_OK;
    },
    setParam(_path, _value) {
      return AGS_OK;
    },
    pushEvents(_bytes) {
      return AGS_OK;
    },
    processBlock(_out, _frames, _channels) {
      return AGS_OK;
    },
    renderOffline(_patch, _events, _sampleRate, _frames, _out) {
      return AGS_OK;
    },
    start() {
      return AGS_OK;
    },
    stop() {
      return AGS_OK;
    },
    saveState(_out) {
      return AGS_OK;
    },
    loadState(_bytes) {
      return AGS_OK;
    },
    recreate(_sampleRate, _maxBlock) {
      return AGS_OK;
    },
  };
}

export function install() {
  globalThis.__AgsynthHost = createMockHost();
  return true;
}

const isMain = Boolean(process.argv[1]) && import.meta.url === pathToFileURL(process.argv[1]).href;

if (isMain) {
  const ok = install();
  const host = globalThis.__AgsynthHost;
  const patchBytes = new ArrayBuffer(828);
  const eventBytes = new ArrayBuffer(12);
  const out = new ArrayBuffer(256 * 2 * 4);
  const status = host.setPatch(patchBytes);
  host.setParam('filter.cutoff_hz', 1200);
  host.pushEvents(eventBytes);
  host.processBlock(out, 256, 2);
  host.renderOffline(patchBytes, eventBytes, 48000, 256, out);
  host.start();
  host.saveState(patchBytes);
  host.loadState(patchBytes);
  host.stop();
  host.recreate(48000);
  host.destroy();
  console.log(
    JSON.stringify({
      installed: ok,
      global: '__AgsynthHost',
      methods: Object.keys(host).sort(),
      setPatch: status,
    }),
  );
}
