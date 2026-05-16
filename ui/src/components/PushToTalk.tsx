import { useCallback, useEffect, useRef, useState } from 'react';

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

// Failure-state copy (Phase 9 §18). The brand never apologises, never
// blames the user, never leaks the exception class name. It states what
// happened and what to do next, in calm grey — not red.
type MicFailure = 'permission' | 'no-device' | 'audio-init' | 'resample';
const MIC_FAILURE_COPY: Record<MicFailure, string> = {
  permission: "Can't hear you — check mic permissions in System Settings.",
  'no-device': "Can't hear you — no microphone detected.",
  'audio-init': "Audio pipeline didn't start. Try again in a moment.",
  resample: "Audio capture finished but couldn't be processed. Try again.",
};

function openSystemSettings() {
  // Phase 15 fix: window.open(..., '_self') navigates the WKWebView itself
  // to x-apple: which the WebView can't handle ("unsupported URL" → fallback
  // page). Route through the native bridge so juce::URL → NSWorkspace handles
  // the URL with the OS. Falls back silently when no bridge (browser/dev).
  const isMac =
    typeof navigator !== 'undefined' &&
    /Mac/i.test(navigator.platform || navigator.userAgent || '');
  const url = isMac
    ? 'x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone'
    : '';
  if (!url) {
    // eslint-disable-next-line no-console
    console.info('[TIMBRE] Open Settings: please enable microphone access.');
    return;
  }
  const juce = (window as unknown as { __JUCE__?: { backend?: { emitEvent?: (n: string, p: unknown) => void } } }).__JUCE__;
  if (juce?.backend?.emitEvent) {
    juce.backend.emitEvent('__juce__invoke', {
      name: 'open_external_url',
      params: [url],
      resultId: 0,
    });
    return;
  }
  // Browser/dev fallback (not in JUCE WebView): open in new tab.
  try {
    window.open(url, '_blank', 'noopener');
  } catch (e) {
    // eslint-disable-next-line no-console
    console.info('[TIMBRE] Open Settings failed silently:', e);
  }
}

export function PushToTalk({ onData, wsReady }: PushToTalkProps) {
  const [state, setState] = useState<RecordState>('idle');
  // Failure flag drives the calm subtitle; null = healthy.
  const [failure, setFailure] = useState<MicFailure | null>(null);
  const audioCtxRef = useRef<AudioContext | null>(null);
  const sourceRef = useRef<MediaStreamAudioSourceNode | null>(null);
  const workletRef = useRef<AudioWorkletNode | null>(null);
  const chunksRef = useRef<Float32Array[]>([]);
  const streamRef = useRef<MediaStream | null>(null);

  // Phase 28 — pre-press permission probe. Without this the user only
  // discovers "mic denied" after holding the button, hits silence, and has
  // no idea why. Permissions API on WebKit/JUCE WebView is partial: the
  // 'microphone' PermissionName isn't in all builds — wrap in try/catch and
  // silently no-op when unsupported, leaving the post-press path as the
  // fallback discovery route.
  useEffect(() => {
    type PermsLike = { query: (d: { name: string }) => Promise<PermissionStatus> };
    const perms = (navigator as unknown as { permissions?: PermsLike }).permissions;
    if (!perms?.query) return;
    let status: PermissionStatus | null = null;
    const apply = (s: PermissionState) => {
      if (s === 'denied') setFailure('permission');
      else if (s === 'granted') setFailure((prev) => (prev === 'permission' ? null : prev));
    };
    perms
      .query({ name: 'microphone' })
      .then((s) => {
        status = s;
        apply(s.state);
        s.onchange = () => apply(s.state);
      })
      .catch(() => {
        // WebKit without 'microphone' PermissionName — fall back to
        // post-press discovery. Not an error path; intentional silence.
      });
    return () => {
      if (status) status.onchange = null;
    };
  }, []);

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
    setFailure(null);
    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    } catch (e) {
      // Classify into the small brand-defined failure taxonomy. We drop
      // the original exception class name from the UI on purpose — it's
      // a developer leak. The original error is still loggable here if
      // anyone needs to debug.
      const name = e instanceof Error ? e.name : 'MicError';
      // eslint-disable-next-line no-console
      console.debug('[TIMBRE] PushToTalk getUserMedia failed:', name, e);
      const kind: MicFailure =
        name === 'NotAllowedError' ? 'permission'
        : name === 'NotFoundError' ? 'no-device'
        : 'permission';
      setFailure(kind);
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
      // eslint-disable-next-line no-console
      console.debug('[TIMBRE] PushToTalk audio init failed:', e);
      setFailure('audio-init');
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
      // eslint-disable-next-line no-console
      console.debug('[TIMBRE] PushToTalk resample failed:', e);
      setFailure('resample');
    }

    audioCtxRef.current = null;
    await ctx.close().catch(() => {});
    setState('idle');
  }, [state, onData]);

  // In a failure state, the button still renders so the user sees the
  // affordance — but with a struck-through mic icon, calm grey subtitle,
  // and an inline "Open Settings" link below. The button is disabled so
  // clicking it isn't a no-op trap; clicking the link routes to System
  // Settings on macOS (best-effort) or just logs.
  const isMicFailure = failure === 'permission' || failure === 'no-device';

  const label =
    state === 'recording' ? 'Release to send'
    : state === 'processing' ? 'Transcribing...'
    : isMicFailure ? "Can't hear you"
    : 'Hold to speak';

  return (
    <div className="ptt-host">
      <button
        type="button"
        className={`ptt-btn${state === 'recording' ? ' ptt-active' : ''}${isMicFailure ? ' ptt-mic-off' : ''}`}
        disabled={!wsReady || state === 'processing' || isMicFailure}
        aria-label={label}
        onMouseDown={start}
        onMouseUp={stop}
        onMouseLeave={stop}
        onTouchStart={(e) => { e.preventDefault(); void start(); }}
        onTouchEnd={(e) => { e.preventDefault(); void stop(); }}
      >
        {isMicFailure && (
          <span className="ptt-mic-icon" aria-hidden="true">
            <span className="ptt-mic-glyph">🎤</span>
            <span className="ptt-mic-strike" />
          </span>
        )}
        <span className="ptt-btn-label">{label}</span>
      </button>
      {failure && (
        <div className="ptt-subtitle" role="status" aria-live="polite">
          <span>{MIC_FAILURE_COPY[failure]}</span>
          {isMicFailure && (
            <>
              {' '}
              <button
                type="button"
                className="ptt-open-settings"
                onClick={openSystemSettings}
              >
                Open Settings
              </button>
            </>
          )}
        </div>
      )}
    </div>
  );
}
