import { useCallback, useRef, useState } from 'react';

interface PushToTalkProps {
  onData: (buf: ArrayBuffer) => void;
  wsReady: boolean;
}

type RecordState = 'idle' | 'recording' | 'processing';

export function PushToTalk({ onData, wsReady }: PushToTalkProps) {
  const [state, setState] = useState<RecordState>('idle');
  const audioCtxRef = useRef<AudioContext | null>(null);
  const sourceRef = useRef<MediaStreamAudioSourceNode | null>(null);
  const processorRef = useRef<ScriptProcessorNode | null>(null);
  const chunksRef = useRef<Float32Array[]>([]);
  const streamRef = useRef<MediaStream | null>(null);

  const start = useCallback(async () => {
    if (state !== 'idle') return;
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      streamRef.current = stream;
      const ctx = new AudioContext();
      audioCtxRef.current = ctx;
      const src = ctx.createMediaStreamSource(stream);
      sourceRef.current = src;
      const proc = ctx.createScriptProcessor(4096, 1, 1);
      processorRef.current = proc;
      chunksRef.current = [];

      proc.onaudioprocess = (e) => {
        chunksRef.current.push(new Float32Array(e.inputBuffer.getChannelData(0)));
      };

      src.connect(proc);
      proc.connect(ctx.destination);
      setState('recording');
    } catch {
      setState('idle');
    }
  }, [state]);

  const stop = useCallback(async () => {
    if (state !== 'recording') return;
    setState('processing');

    const proc = processorRef.current;
    const src = sourceRef.current;
    const ctx = audioCtxRef.current;
    const stream = streamRef.current;

    proc?.disconnect();
    processorRef.current = null;
    src?.disconnect();
    sourceRef.current = null;
    stream?.getTracks().forEach((t) => t.stop());
    streamRef.current = null;

    const chunks = chunksRef.current;
    const totalLen = chunks.reduce((s, c) => s + c.length, 0);

    if (totalLen === 0 || !ctx) {
      await ctx?.close().catch(() => {});
      audioCtxRef.current = null;
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
      Math.ceil((totalLen * targetRate) / ctx.sampleRate),
      targetRate,
    );
    const buf = offCtx.createBuffer(1, totalLen, ctx.sampleRate);
    buf.getChannelData(0).set(pcm);
    const s = offCtx.createBufferSource();
    s.buffer = buf;
    s.connect(offCtx.destination);
    s.start();

    try {
      const rendered = await offCtx.startRendering();
      const raw = rendered.getChannelData(0);
      onData(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength));
    } catch {
      // audio pipeline error — drop silently
    }

    await ctx.close().catch(() => {});
    audioCtxRef.current = null;
    setState('idle');
  }, [state, onData]);

  const label =
    state === 'recording' ? 'Release to send'
    : state === 'processing' ? 'Transcribing...'
    : 'Hold to speak';

  return (
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
  );
}
