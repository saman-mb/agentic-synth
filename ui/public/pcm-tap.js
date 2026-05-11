/* eslint-disable no-undef */
// AudioWorkletProcessor + registerProcessor live in AudioWorkletGlobalScope,
// not the regular browser global scope ESLint is configured for. The
// disable above is scope-correct: this file is loaded via
// AudioWorklet.addModule() and never imported by main-thread code.

// AudioWorkletProcessor for PCM capture.
//
// Replaces the deprecated ScriptProcessorNode mic-tap path. Lives in
// /public/ so Vite serves it as a raw module URL — AudioWorklet.addModule()
// needs a real fetchable URL and *will not* work with bundle-inlined code
// because the worklet runs in its own AudioWorkletGlobalScope worker.
//
// Behavior:
//   - reads input channel 0 every render quantum (128 samples on every
//     modern browser; the spec lets the UA pick, but 128 is universal).
//   - accumulates into a 1024-sample chunk buffer (≈21 ms @ 48 kHz).
//   - posts each filled chunk to the main thread as a Float32Array.
//   - main thread is responsible for resampling 48k → 16k for Whisper
//     (already done in PushToTalk via OfflineAudioContext).
//
// 1024 was picked to roughly match the legacy ScriptProcessorNode buffer
// of 4096 in throughput (4× quanta) while keeping per-chunk message
// overhead low and round-trip latency under one frame.

const CHUNK_SIZE = 1024;

class PcmTapProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this._buffer = new Float32Array(CHUNK_SIZE);
    this._writeIndex = 0;
  }

  process(inputs) {
    const input = inputs[0];
    if (!input || input.length === 0) return true;
    const channel = input[0];
    if (!channel) return true;

    for (let i = 0; i < channel.length; i++) {
      this._buffer[this._writeIndex++] = channel[i];
      if (this._writeIndex >= CHUNK_SIZE) {
        // Send a COPY — postMessage with a transferable would be cheaper
        // but we keep the persistent buffer to avoid per-chunk allocation
        // in the realtime audio render thread.
        const out = new Float32Array(this._buffer);
        this.port.postMessage(out);
        this._writeIndex = 0;
      }
    }
    return true;
  }
}

registerProcessor('pcm-tap', PcmTapProcessor);
