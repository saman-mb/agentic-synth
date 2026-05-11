import { useCallback, useRef, useState } from 'react';

interface PushToTalkProps {
  onData: (buf: ArrayBuffer) => void;
  wsReady: boolean;
}

type RecordState = 'idle' | 'recording' | 'processing';

// pcm-tap.js lives in ui/public/ and is copied verbatim to the build root.
// Root-absolute path (NOT relative) so sub-path deploys — e.g. the plugin
// WebView served at https://agenticsynth.local/app/ — don't resolve this
// against the document URL and 404. We don't use new URL(..., import.meta.url)
// here because the worklet must remain a standalone fetchable module, not a
// bundle-inlined chunk.
const PUBLIC_WORKLET_PATH = '/pcm-tap.js';

export function PushToTalk({ onData, wsReady }: PushToTalkProps) {
  const [state, setState] = useState<RecordState>('idle');
  const [error, setError] = useState<string | null>(null);
  const audioCtxRef = useRef<AudioContext | null>(null);
  const sourceRef = useRef<MediaStreamAudioSourceNode | null>(null);
  const workletRef = useRef<AudioWorkletNode | null>(null);
  const chunksRef = useRef<Float32Array[]>([]);
  const streamRef = useRef<MediaStream | null>(null);

  const teardown = useCallback(async () => {
    workletRef.current?.port.close();
    workletRef.current?.disconnect();
    workletRef.current = null;
    sourceRef.current?.disconnect();
    sourceRef.current = null;
    streamRef.current?.getTracks().forEach((t) => t.stop());
    streamRef.current = null;
    const ctx = audioCtxRef.current;
    audioCtxRef.current = null;
    await ctx?.close().catch(() => {});
  }, []);

  const start = useCallback(async () => {
    if (state !== 'idle') return;
    setError(null);
    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    } catch (e) {
      // Most common: NotAllowedError (user denied) or NotFoundError (no mic).
      // Surface to the UI instead of dropping silently.
      const name = e instanceof Error ? e.name : 'MicError';
      const message =
        name === 'NotAllowedError'
          ? 'Microphone permission denied — enable in browser settings.'
          : name === 'NotFoundError'
            ? 'No microphone detected.'
            : `Microphone unavailable (${name}).`;
      setError(message);
      setState('idle');
      return;
    }
    streamRef.current = stream;
    try {
      const ctx = new AudioContext();
      audioCtxRef.current = ctx;
      // addModule must run before constructing the AudioWorkletNode.
      await ctx.audioWorklet.addModule(PUBLIC_WORKLET_PATH);
      const src = ctx.createMediaStreamSource(stream);
      sourceRef.current = src;
      const node = new AudioWorkletNode(ctx, 'pcm-tap', {
        numberOfInputs: 1,
        numberOfOutputs: 0,
        channelCount: 1,
        channelCountMode: 'explicit',
      });
      workletRef.current = node;
      chunksRef.current = [];
      node.port.onmessage = (e: MessageEvent<Float32Array>) => {
        chunksRef.current.push(e.data);
      };
      src.connect(node);
      // No destination connection needed: the worklet has zero outputs and
      // the WebAudio graph still pulls inputs on it because we hold a
      // strong reference and the source is connected.
      setState('recording');
    } catch (e) {
      const message = e instanceof Error ? e.message : 'audio init failed';
      setError(`Audio pipeline failed: ${message}`);
      await teardown();
      setState('idle');
    }
  }, [state, teardown]);

  const stop = useCallback(async () => {
    if (state !== 'recording') return;
    setState('processing');

    // Capture chunks + sample rate before tearing down the context.
    const ctx = audioCtxRef.current;
    const chunks = chunksRef.current;
    chunksRef.current = [];
    const sourceRate = ctx?.sampleRate ?? 48000;

    // Stop the mic + worklet immediately to release the indicator dot.
    workletRef.current?.disconnect();
    workletRef.current = null;
    sourceRef.current?.disconnect();
    sourceRef.current = null;
    streamRef.current?.getTracks().forEach((t) => t.stop());
    streamRef.current = null;

    const totalLen = chunks.reduce((s, c) => s + c.length, 0);
    if (totalLen === 0 || !ctx) {
      audioCtxRef.current = null;
      await ctx?.close().catch(() => {});
      setState('idle');
      return;
    }

    const pcm = new Float32Array(totalLen);
    let off = 0;
    for (const c of chunks) {
      pcm.set(c, off);
      off += c.length;
    }

    const targetRate = 16000;
    const offCtx = new OfflineAudioContext(
      1,
      Math.max(1, Math.ceil((totalLen * targetRate) / sourceRate)),
      targetRate,
    );
    const buf = offCtx.createBuffer(1, totalLen, sourceRate);
    buf.getChannelData(0).set(pcm);
    const s = offCtx.createBufferSource();
    s.buffer = buf;
    s.connect(offCtx.destination);
    s.start();

    try {
      const rendered = await offCtx.startRendering();
      const raw = rendered.getChannelData(0);
      onData(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength));
    } catch (e) {
      const message = e instanceof Error ? e.message : 'render failed';
      setError(`Audio resample failed: ${message}`);
    }

    audioCtxRef.current = null;
    await ctx.close().catch(() => {});
    setState('idle');
  }, [state, onData]);

  const label =
    state === 'recording' ? 'Release to send'
    : state === 'processing' ? 'Transcribing...'
    : 'Hold to speak';

  return (
    <>
      <button
        type="button"
        className={`ptt-btn${state === 'recording' ? ' ptt-active' : ''}`}
        disabled={!wsReady || state === 'processing'}
        aria-label={label}
        onMouseDown={start}
        onMouseUp={stop}
        onMouseLeave={stop}
        onTouchStart={(e) => { e.preventDefault(); void start(); }}
        onTouchEnd={(e) => { e.preventDefault(); void stop(); }}
      >
        {label}
      </button>
      {error && (
        <div className="ptt-error" role="alert" aria-live="assertive">
          {error}
        </div>
      )}
    </>
  );
}
