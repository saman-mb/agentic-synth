const PROCESSOR_NAME = 'agsynth-worklet';
const CHANNELS = 2;
const MIN_BLOCK = 128;
const MAX_BLOCK = 8192;
const AGS_EVENT_NOTE_ON = 1;
const AGS_EVENT_NOTE_OFF = 2;
const EVENT_SIZE = 12;

// AudioWorkletGlobalScope has no TextEncoder in Chromium or Firefox
// (WebKit is the odd one out), so the NUL-terminated encoding is done by
// hand. Parameter paths are ASCII, but the full range is handled anyway.
function encodeUtf8(str) {
  const out = [];
  for (let i = 0; i < str.length; i++) {
    let cp = str.codePointAt(i);
    if (cp > 0xffff) i++;
    if (cp < 0x80) {
      out.push(cp);
    } else if (cp < 0x800) {
      out.push(0xc0 | (cp >> 6), 0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      out.push(0xe0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f));
    } else {
      out.push(
        0xf0 | (cp >> 18),
        0x80 | ((cp >> 12) & 0x3f),
        0x80 | ((cp >> 6) & 0x3f),
        0x80 | (cp & 0x3f),
      );
    }
  }
  out.push(0);
  return new Uint8Array(out);
}

function writeUtf8(mod, str) {
  const bytes = encodeUtf8(str);
  const ptr = mod._malloc(bytes.length);
  if (!ptr) return 0;
  mod.HEAPU8.set(bytes, ptr);
  return ptr;
}

function writeEvent(mod, kind, note, velocity) {
  const ptr = mod._malloc(EVENT_SIZE);
  if (!ptr) return 0;
  const heap = mod.HEAPU8;
  heap[ptr + 0] = kind & 0xff;
  heap[ptr + 1] = (kind >>> 8) & 0xff;
  heap[ptr + 2] = (kind >>> 16) & 0xff;
  heap[ptr + 3] = (kind >>> 24) & 0xff;
  heap[ptr + 4] = note & 0xff;
  heap[ptr + 5] = velocity & 0xff;
  heap[ptr + 6] = 0;
  heap[ptr + 7] = 0;
  heap[ptr + 8] = 0;
  heap[ptr + 9] = 0;
  heap[ptr + 10] = 0;
  heap[ptr + 11] = 0;
  return ptr;
}

class AgsynthProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super(options);
    // Asset URLs come from the main thread: this scope has no `location`
    // and, outside WebKit, no `URL` to resolve them with.
    const opts = (options && options.processorOptions) || {};
    this.glueUrl = typeof opts.glueUrl === 'string' ? opts.glueUrl : '';
    this.wasmUrl = typeof opts.wasmUrl === 'string' ? opts.wasmUrl : '';
    this.mod = null;
    this.engine = 0;
    this.renderPtr = 0;
    this.maxBlock = MIN_BLOCK;
    this.ready = false;
    this.disposed = false;
    this.port.onmessage = (ev) => this.onMessage(ev.data);
    void this.boot();
  }

  async boot() {
    try {
      const maxBlock = Math.min(MAX_BLOCK, Math.max(MIN_BLOCK, 128));
      if (!this.glueUrl) throw new Error('agsynth glue URL was not supplied');
      // NOTE: import() is disallowed in AudioWorkletGlobalScope in every
      // engine, and the Emscripten ES6 glue needs fetch/URL which are also
      // absent here, so this path cannot succeed until the loader is
      // reworked to instantiate a main-thread-compiled WebAssembly.Module.
      // Failing here is safe: the host probes this worklet and falls back
      // to the pure-WebAudio engine. See issue #404.
      const glue = await import(this.glueUrl);
      const createAgsynthModule = glue.default ?? glue.createAgsynthModule;
      if (typeof createAgsynthModule !== 'function') {
        throw new Error(`createAgsynthModule missing from ${this.glueUrl}`);
      }
      const wasmUrl = this.wasmUrl;
      const mod = await createAgsynthModule({
        locateFile(file) {
          if (String(file).endsWith('.wasm') && wasmUrl) return wasmUrl;
          return `/${file}`;
        },
      });
      const engine = mod._ags_engine_create(sampleRate, maxBlock);
      if (!engine) {
        this.port.postMessage({ type: 'error', message: 'ags_engine_create returned 0' });
        return;
      }
      const renderPtr = mod._malloc(maxBlock * CHANNELS * 4);
      if (!renderPtr) {
        mod._ags_engine_destroy(engine);
        this.port.postMessage({ type: 'error', message: 'ags_engine_render malloc failed' });
        return;
      }
      this.mod = mod;
      this.engine = engine;
      this.renderPtr = renderPtr;
      this.maxBlock = maxBlock;
      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      this.port.postMessage({ type: 'error', message });
    }
  }

  onMessage(msg) {
    if (!msg || typeof msg !== 'object') return;
    if (msg.type === 'dispose') {
      this.destroyEngine();
      return;
    }
    if (!this.ready || !this.mod || !this.engine) return;
    if (msg.type === 'setPatch') {
      this.setPatch(msg.bytes);
      return;
    }
    if (msg.type === 'setParam') {
      this.setParam(msg.path, msg.value);
      return;
    }
    if (msg.type === 'noteOn') {
      this.pushEvent(AGS_EVENT_NOTE_ON, msg.note, msg.velocity);
      return;
    }
    if (msg.type === 'noteOff') {
      this.pushEvent(AGS_EVENT_NOTE_OFF, msg.note, 0);
    }
  }

  setPatch(bytes) {
    if (!bytes) return;
    const src = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : new Uint8Array(bytes);
    const ptr = this.mod._malloc(src.byteLength);
    if (!ptr) return;
    this.mod.HEAPU8.set(src, ptr);
    this.mod._ags_engine_set_patch(this.engine, ptr, src.byteLength);
    this.mod._free(ptr);
  }

  setParam(path, value) {
    if (typeof path !== 'string' || typeof value !== 'number') return;
    const ptr = writeUtf8(this.mod, path);
    if (!ptr) return;
    this.mod._ags_engine_set_param(this.engine, ptr, value);
    this.mod._free(ptr);
  }

  pushEvent(kind, note, velocity) {
    const ptr = writeEvent(this.mod, kind, note | 0, velocity | 0);
    if (!ptr) return;
    this.mod._ags_engine_push_events(this.engine, ptr, 1);
    this.mod._free(ptr);
  }

  destroyEngine() {
    this.ready = false;
    this.disposed = true;
    const mod = this.mod;
    const engine = this.engine;
    const renderPtr = this.renderPtr;
    this.mod = null;
    this.engine = 0;
    this.renderPtr = 0;
    if (!mod) return;
    if (renderPtr) mod._free(renderPtr);
    if (engine) mod._ags_engine_destroy(engine);
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!output || !output[0]) return !this.disposed;
    const frames = output[0].length;
    if (!this.ready || !this.mod || !this.engine) {
      for (let c = 0; c < output.length; c++) output[c].fill(0);
      return !this.disposed;
    }
    let remaining = frames;
    let destOff = 0;
    while (remaining > 0) {
      const n = Math.min(remaining, this.maxBlock);
      this.mod._ags_engine_render(this.engine, this.renderPtr, n, CHANNELS);
      const heap = this.mod.HEAPF32;
      const base = this.renderPtr >> 2;
      const left = output[0];
      const right = output[1] ?? output[0];
      for (let i = 0; i < n; i++) {
        left[destOff + i] = heap[base + i * 2];
        right[destOff + i] = heap[base + i * 2 + 1];
      }
      remaining -= n;
      destOff += n;
    }
    return !this.disposed;
  }
}

registerProcessor(PROCESSOR_NAME, AgsynthProcessor);
