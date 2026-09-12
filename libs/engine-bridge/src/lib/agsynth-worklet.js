const PROCESSOR_NAME = 'agsynth-worklet';
const CHANNELS = 2;
const MIN_BLOCK = 128;
const MAX_BLOCK = 8192;
const AGS_EVENT_NOTE_ON = 1;
const AGS_EVENT_NOTE_OFF = 2;
const EVENT_SIZE = 12;

function writeUtf8(mod, str) {
  const bytes = new TextEncoder().encode(`${str}\0`);
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
  constructor() {
    super();
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
      const glueHref = new URL('agsynth.js', `${self.location.origin}/`).href;
      const glue = await import(glueHref);
      const createAgsynthModule = glue.default ?? glue.createAgsynthModule;
      if (typeof createAgsynthModule !== 'function') {
        throw new Error('createAgsynthModule missing from /agsynth.js');
      }
      const mod = await createAgsynthModule({
        locateFile(file) {
          if (String(file).endsWith('.wasm')) return '/agsynth.wasm';
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
