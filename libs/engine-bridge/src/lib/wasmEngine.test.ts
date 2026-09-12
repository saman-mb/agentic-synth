import { registerHooks } from 'node:module';
import path from 'node:path';
import { afterEach, describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const aliases: Record<string, string> = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
};

function hasJsExt(specifier: string): boolean {
  return /\.[cm]?[jt]sx?$/.test(specifier);
}

registerHooks({
  resolve(specifier, context, nextResolve) {
    const mapped = aliases[specifier];
    if (mapped) return nextResolve(pathToFileURL(mapped).href, context);
    if (!specifier.startsWith('node:') && !hasJsExt(specifier) && (specifier.startsWith('.') || specifier.startsWith('/'))) {
      try {
        return nextResolve(`${specifier}.ts`, context);
      } catch {
        // fall through to the original specifier
      }
    }
    return nextResolve(specifier, context);
  },
});

const { createSynthEngine, WebSynthEngine } = await import('./engine.ts');
const { WasmSynthEngine } = await import('./wasmEngine.ts');

type AudioGlobals = {
  AudioContext?: unknown;
  AudioWorkletNode?: unknown;
};

function audioGlobals(): AudioGlobals {
  return globalThis as unknown as AudioGlobals;
}

function restoreAudioGlobals(prev: { AudioContext: unknown; AudioWorkletNode: unknown }): void {
  const g = audioGlobals();
  if (prev.AudioContext === undefined) delete g.AudioContext;
  else g.AudioContext = prev.AudioContext;
  if (prev.AudioWorkletNode === undefined) delete g.AudioWorkletNode;
  else g.AudioWorkletNode = prev.AudioWorkletNode;
}

function snapshotAudioGlobals(): { AudioContext: unknown; AudioWorkletNode: unknown } {
  const g = audioGlobals();
  return { AudioContext: g.AudioContext, AudioWorkletNode: g.AudioWorkletNode };
}

class FakeAnalyser {
  fftSize = 2048;
  connect(): this {
    return this;
  }
  disconnect(): void {}
  getFloatTimeDomainData(buf: Float32Array): void {
    buf.fill(0);
  }
}

class FakePort {
  private readonly listeners: Array<(ev: { data: unknown }) => void> = [];
  addEventListener(type: string, fn: (ev: { data: unknown }) => void): void {
    if (type === 'message') this.listeners.push(fn);
  }
  removeEventListener(type: string, fn: (ev: { data: unknown }) => void): void {
    if (type !== 'message') return;
    const i = this.listeners.indexOf(fn);
    if (i >= 0) this.listeners.splice(i, 1);
  }
  start(): void {}
  postMessage(_msg?: unknown, _transfer?: unknown): void {}
  emit(data: unknown): void {
    for (const fn of [...this.listeners]) fn({ data });
  }
}

class FakeWorkletNode {
  readonly port = new FakePort();
  constructor() {
    queueMicrotask(() => {
      this.port.emit({ type: 'error', message: 'Failed to fetch /agsynth.wasm' });
    });
  }
  connect(): this {
    return this;
  }
  disconnect(): void {}
}

class FakeAudioContext {
  state: string = 'running';
  destination = {};
  audioWorklet = {
    addModule: async (): Promise<void> => {
      throw new Error('Failed to execute addModule on AudioWorklet');
    },
  };
  createAnalyser(): FakeAnalyser {
    return new FakeAnalyser();
  }
  close(): Promise<void> {
    this.state = 'closed';
    return Promise.resolve();
  }
  resume(): Promise<void> {
    this.state = 'running';
    return Promise.resolve();
  }
}

describe('WasmSynthEngine', () => {
  const prev = snapshotAudioGlobals();

  afterEach(() => {
    restoreAudioGlobals(prev);
  });

  it('ensureStarted rejects when addModule fails', async () => {
    const g = audioGlobals();
    g.AudioContext = FakeAudioContext;
    const engine = new WasmSynthEngine();
    await assert.rejects(() => engine.ensureStarted(), (err: unknown) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /addModule/i);
      return true;
    });
  });

  it('ensureStarted rejects when the worklet reports a fetch/instantiate failure', async () => {
    class FetchFailContext extends FakeAudioContext {
      audioWorklet = {
        addModule: async (): Promise<void> => {},
      };
    }
    const g = audioGlobals();
    g.AudioContext = FetchFailContext;
    g.AudioWorkletNode = FakeWorkletNode;
    const engine = new WasmSynthEngine();
    await assert.rejects(() => engine.ensureStarted(), (err: unknown) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /agsynth\.wasm|fetch|instantiate/i);
      return true;
    });
  });

  it('createSynthEngine() returns an object with ensureStarted', () => {
    const engine = createSynthEngine();
    assert.equal(typeof engine.ensureStarted, 'function');
    assert.ok(engine instanceof WasmSynthEngine);
  });

  it('WebSynthEngine is still constructible', () => {
    const engine = new WebSynthEngine();
    assert.equal(typeof engine.ensureStarted, 'function');
    assert.equal(engine instanceof WebSynthEngine, true);
  });
});
