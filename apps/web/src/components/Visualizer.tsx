import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  computeSpectrumBands,
  dbToNorm,
  detectFundamental,
  frequencyToX,
  lissajousPoint,
  parseScopeFrame,
  SPECTRUM_FFT_SIZE,
} from '@agentic-synth/engine-bridge';
import './Visualizer.css';

// ── Visualizer (Phase 5 + Phase 12 + #434/#435/#436) ────────────────
//
// Canvas-based oscilloscope / spectrum / XY / wavetable view.
//
// Since #434/#435/#436 the component shows *real* audio or nothing:
//  • The bridge pulls a contiguous, interleaved-stereo window per RAF (#436).
//  • SCOPE/SPECTRUM/WT draw the left channel; XY draws real L against real R,
//    where L and R reach us in one non-mono-summed frame (#435).
//  • The spectrum is normalised to dBFS over a documented -90..0 dBFS window
//    and scaled by the engine's real sample rate (#434).
//  • The wavetable cycle length comes from autocorrelation pitch detection on
//    the actual audio — never a hardcoded 220 Hz (#435).
//
// There is no simulated fallback. When no real source is connected (or the
// producer overran the window) every mode draws an explicit "no signal" state
// instead of a convincing fake.

const SAMPLE_COUNT = SPECTRUM_FFT_SIZE; // 1024 frames
const DEFAULT_SAMPLE_RATE = 48_000; // only until the bridge reports the real one

// JUCE bridge shapes — same wire format as useWebSocket.ts / useSynthBridge.ts.
// We talk to the `getScopeSamples` native function by emitting __juce__invoke
// with a positional-args params array and listening for __juce__complete
// keyed by a numeric promiseId. ID-namespaced offset (1_000_000+) matches
// useWebSocket.ts so we never collide with the bundled getNativeFunction
// handler's IDs (which start from 0).
interface JuceBackendForScope {
  emitEvent: (name: string, payload: unknown) => void;
  addEventListener: (name: string, cb: (payload: unknown) => void) => number;
  removeEventListener: (id: number) => void;
}
interface JuceGlobalForScope {
  backend: JuceBackendForScope;
}
function getJuceForScope(): JuceGlobalForScope | null {
  const j = (window as unknown as { __JUCE__?: JuceGlobalForScope }).__JUCE__;
  return j ?? null;
}

// Evaluated per call: module-scope capture ran before demo/bootstrap.ts
// installed the shim (#280).
const scopeBridgeAvailable = (): boolean => getJuceForScope() !== null;

// Module-scope promise plumbing for the scope pull. Module-scope (not
// component-scope) so a remount doesn't double-register the __juce__complete
// listener. ID offset 2_000_000 keeps our IDs distinct from both JUCE's
// bundled handler (starts at 0) and useWebSocket's pool (starts at 1_000_000).
const SCOPE_PROMISE_ID_OFFSET = 2_000_000;
let nextScopePromiseId = SCOPE_PROMISE_ID_OFFSET;
// One missed completion must not wedge the poll loop: inFlightRef only
// clears when the pull's promise settles, so a lost __juce__complete
// (WebView teardown mid-call, dropped native completion) would otherwise
// stop every future pull — a frozen scope over audible audio. One missed
// frame is invisible; that latch is not. Matches the timeout contract in
// useWebSocket.callNative.
const SCOPE_PULL_TIMEOUT_MS = 1_000;
const pendingScopePromises = new Map<number, (v: unknown) => void>();
let scopeCompleteWired = false;

function ensureScopeCompleteListener(juce: JuceGlobalForScope): void {
  if (scopeCompleteWired) return;
  scopeCompleteWired = true;
  juce.backend.addEventListener('__juce__complete', (payload) => {
    const p = payload as { promiseId: number; result: unknown };
    if (typeof p.promiseId !== 'number' || p.promiseId < SCOPE_PROMISE_ID_OFFSET) return;
    const resolver = pendingScopePromises.get(p.promiseId);
    if (resolver) {
      pendingScopePromises.delete(p.promiseId);
      resolver(p.result);
    }
  });
}

function callGetScopeSamples(n: number): Promise<unknown> | null {
  const juce = getJuceForScope();
  if (!juce) return null;
  ensureScopeCompleteListener(juce);
  const id = nextScopePromiseId++;
  return new Promise<unknown>((resolve) => {
    const timer = window.setTimeout(() => {
      // Settle only if the completion truly never arrived — the complete
      // listener deletes the entry before resolving. Resolving [] makes
      // the caller drop the frame and clear inFlightRef, so the next RAF
      // retries the pull instead of stalling forever.
      if (pendingScopePromises.delete(id)) resolve([]);
    }, SCOPE_PULL_TIMEOUT_MS);
    pendingScopePromises.set(id, (result) => {
      window.clearTimeout(timer);
      // Result is `{ samples, sampleRate, ... }` (current) or an array
      // (older shim). parseScopeFrame normalises both.
      resolve(result);
    });
    juce.backend.emitEvent('__juce__invoke', {
      name: 'getScopeSamples',
      params: [n],
      resultId: id,
    });
  });
}

type Mode = 'SCOPE' | 'SPECTRUM' | 'XY' | 'WT';
const MODES: ReadonlyArray<Mode> = ['SCOPE', 'SPECTRUM', 'XY', 'WT'];

export interface VisualizerProps {
  // Optional real-audio hook for tests/parents: returns up to SAMPLE_COUNT
  // mono float samples per frame. Treated as a real source (left channel);
  // XY then falls back to L == R. The JUCE/browser bridge is preferred when
  // both are present.
  sampleProvider?: () => Float32Array;
}

// ── tiny utilities ────────────────────────────────────────────────────

/** Read a CSS custom property from the closest element with a value. */
function readVar(el: HTMLElement | null, name: string, fallback: string): string {
  if (!el) return fallback;
  const v = getComputedStyle(el).getPropertyValue(name).trim();
  return v || fallback;
}

/** Index of the first rising zero crossing at/after `start` (wraps). */
function findRisingZeroCrossing(samples: Float32Array, start: number): number {
  const n = samples.length;
  for (let i = 1; i < n; i++) {
    const idx = (start + i) % n;
    const prev = (idx - 1 + n) % n;
    if (samples[prev] <= 0 && samples[idx] > 0) return idx;
  }
  return 0;
}

// ── component ─────────────────────────────────────────────────────────

export function Visualizer({ sampleProvider }: VisualizerProps) {
  const [mode, setMode] = useState<Mode>('SCOPE');
  const containerRef = useRef<HTMLDivElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  // Refs we need inside the RAF loop without re-binding it.
  const modeRef = useRef<Mode>(mode);
  modeRef.current = mode;
  const providerRef = useRef<VisualizerProps['sampleProvider']>(sampleProvider);
  providerRef.current = sampleProvider;

  // Phase 12 / #436: bridge-pulled audio. Each RAF kicks off an async pull
  // (resolved by the JUCE message thread); the render loop reads the latest
  // stereo window synchronously. inFlightRef gates re-entry so we never queue
  // a second request before the first resolves. A stale frame (producer
  // overrun) is marked and skipped rather than FFT'd across a discontinuity.
  const scopeLRef = useRef<Float32Array>(new Float32Array(SAMPLE_COUNT));
  const scopeRRef = useRef<Float32Array>(new Float32Array(SAMPLE_COUNT));
  const scopeFilledRef = useRef<boolean>(false);
  const scopeStaleRef = useRef<boolean>(false);
  const scopeSampleRateRef = useRef<number>(DEFAULT_SAMPLE_RATE);
  const inFlightRef = useRef<boolean>(false);

  // Persistent scratch buffers — allocated once, mutated each frame.
  const bufs = useMemo(() => {
    return {
      sample: new Float32Array(SAMPLE_COUNT),    // left / mono scope source
      sampleR: new Float32Array(SAMPLE_COUNT),   // right channel for XY
      peaks: new Float32Array(96),               // spectrum peak-hold
    };
  }, []);

  // ── retina sizing ─────────────────────────────────────────────────
  const resize = useCallback(() => {
    const canvas = canvasRef.current;
    const wrap = containerRef.current;
    if (!canvas || !wrap) return;
    const dpr = window.devicePixelRatio || 1;
    const rect = wrap.getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));
    if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
      canvas.width = w * dpr;
      canvas.height = h * dpr;
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
    }
  }, []);

  useEffect(() => {
    resize();
    const wrap = containerRef.current;
    if (!wrap || typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(() => resize());
    ro.observe(wrap);
    return () => ro.disconnect();
  }, [resize]);

  // ── render loop ───────────────────────────────────────────────────
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    let raf = 0;
    let stopped = false;

    const root = canvas;
    const accentPrimary = readVar(root, '--accent-primary', '#7C4DFF');
    const accentSecondary = readVar(root, '--accent-secondary', '#FF3D88');
    const accentGlow = readVar(root, '--accent-glow', 'rgba(124,77,255,0.45)');
    const bgInset = readVar(root, '--bg-inset', '#07080B');
    const textTertiary = readVar(root, '--text-tertiary', 'rgba(255,255,255,0.45)');
    const gridStroke = 'rgba(255,255,255,0.03)';

    const draw = () => {
      if (stopped) return;
      // A throw inside a frame would otherwise skip the requestAnimationFrame
      // below and kill the loop permanently — a frozen canvas. Log and keep
      // the loop alive instead; the next frame may well succeed.
      try {
        const tNow = performance.now() / 1000;
        const currentMode = modeRef.current;

        // 1. Fire-and-forget bridge pull once per frame.
        if (scopeBridgeAvailable() && !inFlightRef.current) {
          inFlightRef.current = true;
          const p = callGetScopeSamples(SAMPLE_COUNT);
          if (p) {
            p.then((result) => {
              const frame = parseScopeFrame(result, scopeSampleRateRef.current);
              if (!frame) return;
              scopeSampleRateRef.current = frame.sampleRate;
              if (frame.stale) {
                // Producer overran the window mid-copy: skip it entirely.
                scopeStaleRef.current = true;
                return;
              }
              if (frame.samples.length < SAMPLE_COUNT * 2) return; // not enough yet
              const interleaved = frame.samples;
              for (let i = 0; i < SAMPLE_COUNT; i++) {
                scopeLRef.current[i] = interleaved[i * 2];
                scopeRRef.current[i] = interleaved[i * 2 + 1];
              }
              scopeFilledRef.current = true;
              scopeStaleRef.current = false;
            }).finally(() => {
              inFlightRef.current = false;
            });
          } else {
            inFlightRef.current = false;
          }
        }

        // 2. Select the real source. No simulated fallback exists.
        const provider = providerRef.current;
        let signal: 'ok' | 'none' | 'stale' = 'none';
        if (provider) {
          const provided = provider();
          if (provided && provided.length >= SAMPLE_COUNT) {
            bufs.sample.set(provided.subarray(0, SAMPLE_COUNT));
            bufs.sampleR.set(bufs.sample); // mono test hook → L == R
            signal = 'ok';
          }
        } else if (scopeBridgeAvailable() && scopeFilledRef.current) {
          if (scopeStaleRef.current) {
            signal = 'stale';
          } else {
            bufs.sample.set(scopeLRef.current);
            bufs.sampleR.set(scopeRRef.current);
            signal = 'ok';
          }
        }

        const dpr = window.devicePixelRatio || 1;
        const W = canvas.width;
        const H = canvas.height;
        const colors = { accentPrimary, accentSecondary, accentGlow, bgInset, gridStroke, textTertiary };

        if (signal !== 'ok') {
          drawSignalState(ctx, W, H, dpr, colors, signal === 'stale' ? 'SIGNAL OVERRUN' : 'NO SIGNAL');
        } else {
          switch (currentMode) {
            case 'SCOPE':
              drawScope(ctx, W, H, dpr, bufs.sample, colors);
              break;
            case 'SPECTRUM':
              drawSpectrum(ctx, W, H, dpr, bufs, scopeSampleRateRef.current, colors);
              break;
            case 'XY':
              drawXY(ctx, W, H, dpr, bufs.sample, bufs.sampleR, colors);
              break;
            case 'WT':
              drawWavetable(ctx, W, H, dpr, bufs.sample, scopeSampleRateRef.current, tNow, colors);
              break;
          }
        }
      } catch (err) {
        console.warn('[visualizer] draw frame failed:', err);
      }
      raf = requestAnimationFrame(draw);
    };

    raf = requestAnimationFrame(draw);
    return () => {
      stopped = true;
      cancelAnimationFrame(raf);
    };
  }, [bufs]);

  return (
    <div className="visualizer">
      <div className="vz-toggle" role="tablist" aria-label="Visualizer mode">
        {MODES.map((m) => (
          <button
            key={m}
            role="tab"
            type="button"
            aria-selected={mode === m}
            className={`vz-toggle-btn${mode === m ? ' vz-toggle-btn-active' : ''}`}
            onClick={() => setMode(m)}
          >
            {m}
          </button>
        ))}
      </div>
      <div ref={containerRef} className="vz-canvas-wrap">
        <canvas ref={canvasRef} className="vz-canvas" aria-hidden="true" />
      </div>
    </div>
  );
}

// ── per-mode renderers ────────────────────────────────────────────────

interface ScopeColors {
  accentPrimary: string;
  accentSecondary: string;
  accentGlow: string;
  bgInset: string;
  gridStroke: string;
  textTertiary: string;
}

function drawGrid(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  bg: string,
  grid: string,
) {
  // Background well + faint grid (vertical center, horizontal 1/3 marks).
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, W, H);
  ctx.strokeStyle = grid;
  ctx.lineWidth = 1 * dpr;
  ctx.beginPath();
  // vertical centre
  ctx.moveTo(W / 2, 0);
  ctx.lineTo(W / 2, H);
  // horizontal 1/3 marks
  ctx.moveTo(0, H / 3);
  ctx.lineTo(W, H / 3);
  ctx.moveTo(0, (2 * H) / 3);
  ctx.lineTo(W, (2 * H) / 3);
  ctx.stroke();
}

/** Centred status text used when there is no real, contiguous audio. */
function drawSignalState(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  c: ScopeColors,
  label: string,
) {
  drawGrid(ctx, W, H, dpr, c.bgInset, c.gridStroke);
  ctx.fillStyle = c.textTertiary;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.font = `${11 * dpr}px "JetBrains Mono", monospace`;
  ctx.fillText(label, W / 2, H / 2);
}

function drawScope(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  sample: Float32Array,
  c: ScopeColors,
) {
  // Decay trail: draw last frame at 20% before redrawing this one.
  ctx.fillStyle = c.bgInset;
  ctx.globalAlpha = 0.8; // leaves 20% of previous frame visible
  ctx.fillRect(0, 0, W, H);
  ctx.globalAlpha = 1;

  ctx.strokeStyle = c.gridStroke;
  ctx.lineWidth = 1 * dpr;
  ctx.beginPath();
  ctx.moveTo(W / 2, 0);
  ctx.lineTo(W / 2, H);
  ctx.moveTo(0, H / 3);
  ctx.lineTo(W, H / 3);
  ctx.moveTo(0, (2 * H) / 3);
  ctx.lineTo(W, (2 * H) / 3);
  ctx.stroke();

  ctx.shadowColor = c.accentGlow;
  ctx.shadowBlur = 6 * dpr;
  ctx.strokeStyle = c.accentPrimary;
  ctx.lineWidth = 1.5 * dpr;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  const mid = H / 2;
  for (let i = 0; i < SAMPLE_COUNT; i++) {
    const x = (i / (SAMPLE_COUNT - 1)) * W;
    const y = mid - sample[i] * (H * 0.4);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
  ctx.shadowBlur = 0;
}

function drawSpectrum(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  bufs: {
    sample: Float32Array;
    peaks: Float32Array;
  },
  sampleRate: number,
  c: ScopeColors,
) {
  drawGrid(ctx, W, H, dpr, c.bgInset, c.gridStroke);

  const { sample, peaks } = bufs;
  const BANDS = peaks.length; // 96
  // dBFS per log band, clamped to the documented -90..0 dBFS window
  // (see engine-bridge scope.ts). A full-scale sine reads ~0; white noise at
  // -50 dBFS RMS reads well above the floor instead of a sliver.
  const db = computeSpectrumBands(sample, sampleRate, BANDS);

  // Gradient: violet → magenta → white at top.
  const grad = ctx.createLinearGradient(0, H, 0, 0);
  grad.addColorStop(0, c.accentPrimary);
  grad.addColorStop(0.65, c.accentSecondary);
  grad.addColorStop(1, '#FFFFFF');

  const gap = 1 * dpr;
  const bandWidth = (W - gap * (BANDS + 1)) / BANDS;

  for (let b = 0; b < BANDS; b++) {
    const norm = dbToNorm(db[b]);

    // Peak hold — decays ~1s.
    const decay = 0.985;
    peaks[b] = Math.max(norm, peaks[b] * decay);

    const x = gap + b * (bandWidth + gap);
    const barH = norm * (H - 4 * dpr);
    ctx.fillStyle = grad;
    ctx.fillRect(x, H - barH, bandWidth, barH);

    const py = H - peaks[b] * (H - 4 * dpr);
    ctx.fillStyle = 'rgba(255,255,255,0.6)';
    ctx.fillRect(x, py - 1 * dpr, bandWidth, 1 * dpr);
  }

  // Frequency axis: faint octave gridlines + decade labels, positioned with
  // the engine's real sample rate so the axis is honest (#434).
  const fLo = (2 * sampleRate) / SPECTRUM_FFT_SIZE;
  const fHi = ((SPECTRUM_FFT_SIZE / 2 - 1) * sampleRate) / SPECTRUM_FFT_SIZE;
  const octaves = [50, 100, 200, 400, 800, 1600, 3200, 6400, 12800];
  const decades = [100, 1000, 10000];
  ctx.strokeStyle = 'rgba(255,255,255,0.08)';
  ctx.lineWidth = 1 * dpr;
  for (const f of octaves) {
    if (f < fLo || f > fHi) continue;
    const x = frequencyToX(f, sampleRate, W);
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, H);
    ctx.stroke();
  }
  ctx.fillStyle = c.textTertiary;
  ctx.textAlign = 'left';
  ctx.textBaseline = 'bottom';
  ctx.font = `${9 * dpr}px "JetBrains Mono", monospace`;
  for (const f of decades) {
    if (f < fLo || f > fHi) continue;
    const x = frequencyToX(f, sampleRate, W);
    ctx.fillText(f >= 1000 ? `${f / 1000}k` : `${f}`, x + 2 * dpr, H - 2 * dpr);
  }
}

function drawXY(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  L: Float32Array,
  R: Float32Array,
  c: ScopeColors,
) {
  // Fade trail.
  ctx.fillStyle = c.bgInset;
  ctx.globalAlpha = 0.85;
  ctx.fillRect(0, 0, W, H);
  ctx.globalAlpha = 1;

  // Crosshair grid.
  ctx.strokeStyle = c.gridStroke;
  ctx.lineWidth = 1 * dpr;
  ctx.beginPath();
  ctx.moveTo(W / 2, 0);
  ctx.lineTo(W / 2, H);
  ctx.moveTo(0, H / 2);
  ctx.lineTo(W, H / 2);
  ctx.stroke();

  // Lissajous: real left against real right (#435).
  ctx.shadowColor = c.accentGlow;
  ctx.shadowBlur = 4 * dpr;
  ctx.strokeStyle = c.accentPrimary;
  ctx.lineWidth = 1.5 * dpr;
  ctx.beginPath();
  for (let i = 0; i < SAMPLE_COUNT; i++) {
    const { x, y } = lissajousPoint(L[i], R[i], W, H);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
  ctx.shadowBlur = 0;
}

function drawWavetable(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  sample: Float32Array,
  sampleRate: number,
  tNow: number,
  c: ScopeColors,
) {
  // Cycle length from the actual playing fundamental (#435). No hardcoded
  // 220 Hz: if the window is unpitched we say so rather than fake a cycle.
  const { hz, confidence } = detectFundamental(sample, sampleRate);
  drawGrid(ctx, W, H, dpr, c.bgInset, c.gridStroke);
  if (hz <= 0 || confidence < 0.5) {
    ctx.fillStyle = c.textTertiary;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = `${11 * dpr}px "JetBrains Mono", monospace`;
    ctx.fillText('NO PITCH', W / 2, H / 2);
    return;
  }

  const cycleSamples = Math.max(8, Math.min(SAMPLE_COUNT, Math.round(sampleRate / hz)));
  const start = findRisingZeroCrossing(sample, 0);

  // 7 layered curves, animated z-depth shifting — "tunnel of frames".
  const LAYERS = 7;
  const cx = W / 2;
  const cy = H / 2;

  for (let l = LAYERS - 1; l >= 0; l--) {
    const depth = l / (LAYERS - 1);                       // 0..1
    const shift = ((tNow * 0.6) % 1 + 1 + depth) % 1;     // 0..1 looping
    const scale = 0.45 + 0.45 * (1 - shift);              // bigger when close
    const y0 = cy - (1 - shift) * H * 0.18;
    const xw = W * 0.78 * scale;
    const xL = cx - xw / 2;

    const opacity = (1 - shift) * 0.7 + 0.08;
    ctx.strokeStyle = c.accentPrimary;
    ctx.globalAlpha = opacity;
    ctx.lineWidth = 1.5 * dpr;
    ctx.beginPath();
    for (let i = 0; i < cycleSamples; i++) {
      const x = xL + (i / (cycleSamples - 1)) * xw;
      const y = y0 - sample[(start + i) % SAMPLE_COUNT] * H * 0.22 * scale;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }
  ctx.globalAlpha = 1;

  // Honest readout of the detected fundamental driving the cycle length.
  ctx.fillStyle = c.textTertiary;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'bottom';
  ctx.font = `${9 * dpr}px "JetBrains Mono", monospace`;
  ctx.fillText(`${hz.toFixed(1)} Hz`, W - 4 * dpr, H - 3 * dpr);
}
