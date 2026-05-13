import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import './Visualizer.css';

// ── Visualizer (Phase 5 + Phase 12) ─────────────────────────────────
//
// Canvas-based oscilloscope / spectrum / XY / wavetable view.
//
// Phase 12 wired real audio: when running inside the JUCE WebView host the
// component polls the `getScopeSamples` native function once per RAF, stores
// the latest pulled buffer in a ref, and feeds it into the render loop.
// When the bridge is absent (Vite dev / unit tests) we fall back to the
// Phase-5 simulated source so the visuals stay alive.
//
// All modes share:
//   • retina-aware canvas (window.devicePixelRatio)
//   • single requestAnimationFrame loop driven by `mode`
//   • cancelAnimationFrame on unmount (no leaks)

const SAMPLE_COUNT = 1024;
const SAMPLE_RATE = 44_100;           // notional — purely for nice frequencies
const FUNDAMENTAL_HZ = 220;

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

// Module-scope promise plumbing for the scope pull. Module-scope (not
// component-scope) so a remount doesn't double-register the __juce__complete
// listener. ID offset 2_000_000 keeps our IDs distinct from both JUCE's
// bundled handler (starts at 0) and useWebSocket's pool (starts at 1_000_000).
const SCOPE_PROMISE_ID_OFFSET = 2_000_000;
let nextScopePromiseId = SCOPE_PROMISE_ID_OFFSET;
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

function callGetScopeSamples(n: number): Promise<number[]> | null {
  const juce = getJuceForScope();
  if (!juce) return null;
  ensureScopeCompleteListener(juce);
  const id = nextScopePromiseId++;
  return new Promise<number[]>((resolve) => {
    pendingScopePromises.set(id, (result) => {
      // Result may be Array<number> (typical) or undefined (provider unset).
      if (Array.isArray(result)) resolve(result as number[]);
      else resolve([]);
    });
    juce.backend.emitEvent('__juce__invoke', {
      name: 'getScopeSamples',
      params: [n],
      resultId: id,
    });
  });
}

// Bridge presence is fixed for the page lifetime — cache once.
const SCOPE_BRIDGE_AVAILABLE = getJuceForScope() !== null;

type Mode = 'SCOPE' | 'SPECTRUM' | 'XY' | 'WT';
const MODES: ReadonlyArray<Mode> = ['SCOPE', 'SPECTRUM', 'XY', 'WT'];

export interface VisualizerProps {
  // When a real audio pipeline arrives, callers can hand us a fresh
  // 1024-sample float buffer per frame and we'll prefer it over the
  // internal simulation.
  sampleProvider?: () => Float32Array;
}

// ── tiny utilities ────────────────────────────────────────────────────

/** Read a CSS custom property from the closest element with a value. */
function readVar(el: HTMLElement | null, name: string, fallback: string): string {
  if (!el) return fallback;
  const v = getComputedStyle(el).getPropertyValue(name).trim();
  return v || fallback;
}

/** Tiny radix-2 in-place FFT. Real input (imag pre-zeroed). */
function fftInPlace(re: Float32Array, im: Float32Array): void {
  const n = re.length;
  // bit-reverse permutation
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const half = len >> 1;
    const ang = (-2 * Math.PI) / len;
    const wre = Math.cos(ang);
    const wim = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cre = 1;
      let cim = 0;
      for (let k = 0; k < half; k++) {
        const tre = cre * re[i + k + half] - cim * im[i + k + half];
        const tim = cre * im[i + k + half] + cim * re[i + k + half];
        re[i + k + half] = re[i + k] - tre;
        im[i + k + half] = im[i + k] - tim;
        re[i + k] += tre;
        im[i + k] += tim;
        const ncre = cre * wre - cim * wim;
        cim = cre * wim + cim * wre;
        cre = ncre;
      }
    }
  }
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

  // Phase 12: bridge-pulled audio. Each RAF kicks off an async pull (resolved
  // by the JUCE message thread); the render loop reads scopeBufRef.current
  // synchronously. inFlightRef gates re-entry so we never queue a second
  // request before the first resolves — drops a frame's worth of samples
  // rather than letting the queue back up, matching the "lose visualizer
  // frames before blocking" contract from the C++ side.
  const scopeBufRef = useRef<Float32Array>(new Float32Array(SAMPLE_COUNT));
  const scopeFilledRef = useRef<boolean>(false);
  const inFlightRef = useRef<boolean>(false);

  // Persistent scratch buffers — allocated once, mutated each frame.
  const bufs = useMemo(() => {
    return {
      sample: new Float32Array(SAMPLE_COUNT),    // mono / scope / spectrum source
      sampleR: new Float32Array(SAMPLE_COUNT),   // right channel for XY
      fftRe: new Float32Array(SAMPLE_COUNT),
      fftIm: new Float32Array(SAMPLE_COUNT),
      peaks: new Float32Array(96),               // spectrum peak-hold
    };
  }, []);

  // ── simulation ────────────────────────────────────────────────────
  // tNow is in seconds. We synthesise SAMPLE_COUNT samples back-dated
  // from the current time so the trace appears to scroll smoothly.
  const synthesise = useCallback((tNow: number, currentMode: Mode) => {
    const { sample, sampleR } = bufs;
    // Wavetable morph position: 0=sine, 1=saw, 2=square, loops over 8 s.
    const morph = ((tNow / 8) % 3 + 3) % 3;
    const tremolo = 0.5 + 0.5 * Math.sin(2 * Math.PI * 2 * tNow); // 2 Hz LFO

    for (let i = 0; i < SAMPLE_COUNT; i++) {
      const t = tNow + (i - SAMPLE_COUNT) / SAMPLE_RATE;
      const phase = 2 * Math.PI * FUNDAMENTAL_HZ * t;

      // Base sine + a couple of harmonics that grow with morph for
      // SPECTRUM. We keep WT visually distinct by also blending
      // sine→saw→square shapes when in WT mode.
      let s: number;
      if (currentMode === 'WT') {
        const ph = (FUNDAMENTAL_HZ * t) % 1;
        const sine = Math.sin(2 * Math.PI * ph);
        const saw = 2 * ph - 1;
        const square = ph < 0.5 ? 1 : -1;
        let a: number, b: number, w: number;
        if (morph < 1) { a = sine; b = saw; w = morph; }
        else if (morph < 2) { a = saw; b = square; w = morph - 1; }
        else { a = square; b = sine; w = morph - 2; }
        s = a * (1 - w) + b * w;
      } else {
        // SCOPE / SPECTRUM / XY source: sine + tremolo-amount of 2nd & 3rd
        // harmonic + a sprinkle of noise. Harmonics give SPECTRUM bars to
        // render; noise gives the trace texture.
        const h2 = 0.35 * tremolo * Math.sin(2 * phase);
        const h3 = 0.18 * tremolo * Math.sin(3 * phase);
        const noise = (Math.random() - 0.5) * 0.03;
        s = 0.7 * Math.sin(phase) + h2 + h3 + noise;
      }

      sample[i] = s * (0.6 + 0.3 * tremolo);

      // XY right channel: same sine with phase offset → ellipse.
      sampleR[i] = 0.7 * Math.sin(phase + Math.PI / 4 + 0.2 * Math.sin(2 * Math.PI * 0.3 * tNow));
    }
  }, [bufs]);

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

    // Cache theme tokens once per loop start; cheap and avoids
    // hammering getComputedStyle every frame.
    const root = canvas;
    const accentPrimary = readVar(root, '--accent-primary', '#7C4DFF');
    const accentSecondary = readVar(root, '--accent-secondary', '#FF3D88');
    const accentGlow = readVar(root, '--accent-glow', 'rgba(124,77,255,0.45)');
    const bgInset = readVar(root, '--bg-inset', '#07080B');
    const gridStroke = 'rgba(255,255,255,0.03)';

    const draw = () => {
      if (stopped) return;
      const tNow = performance.now() / 1000;
      const currentMode = modeRef.current;

      // 1. Fire-and-forget bridge pull once per frame. The promise resolves
      //    on a future tick; while it does, the render loop reads the most
      //    recent scopeBufRef.current synchronously. inFlightRef gates
      //    re-entry so concurrent RAFs don't pile up requests.
      if (SCOPE_BRIDGE_AVAILABLE && !inFlightRef.current) {
        inFlightRef.current = true;
        const p = callGetScopeSamples(SAMPLE_COUNT);
        if (p) {
          p.then((arr) => {
            if (arr.length > 0) {
              // Shift-and-append into the rolling window. If the producer
              // delivered a full window's worth, do a direct copy; otherwise
              // shift the existing tail left and append the new samples on
              // the right so the trace appears to scroll continuously.
              const buf = scopeBufRef.current;
              if (arr.length >= SAMPLE_COUNT) {
                for (let i = 0; i < SAMPLE_COUNT; i++) buf[i] = arr[arr.length - SAMPLE_COUNT + i];
              } else {
                const keep = SAMPLE_COUNT - arr.length;
                buf.copyWithin(0, arr.length, SAMPLE_COUNT);
                for (let i = 0; i < arr.length; i++) buf[keep + i] = arr[i];
              }
              scopeFilledRef.current = true;
            }
          }).finally(() => {
            inFlightRef.current = false;
          });
        } else {
          inFlightRef.current = false;
        }
      }

      // 2. Fill our sample buffers. Always run synthesise() first so
      //    sampleR (used by XY) and baseline sample are populated, then
      //    overwrite bufs.sample with real audio when available.
      //    Priority order for bufs.sample:
      //      a) explicit sampleProvider prop (parent wiring / tests)
      //      b) JUCE bridge buffer (real audio from C++)
      //      c) simulated source (already in place from synthesise)
      synthesise(tNow, currentMode);
      const provided = providerRef.current?.();
      if (provided && provided.length >= SAMPLE_COUNT) {
        bufs.sample.set(provided.subarray(0, SAMPLE_COUNT));
      } else if (SCOPE_BRIDGE_AVAILABLE && scopeFilledRef.current) {
        bufs.sample.set(scopeBufRef.current);
      }

      const dpr = window.devicePixelRatio || 1;
      const W = canvas.width;
      const H = canvas.height;

      switch (currentMode) {
        case 'SCOPE':
          drawScope(ctx, W, H, dpr, bufs.sample, { accentPrimary, accentGlow, bgInset, gridStroke });
          break;
        case 'SPECTRUM':
          drawSpectrum(ctx, W, H, dpr, bufs, { accentPrimary, accentSecondary, bgInset, gridStroke });
          break;
        case 'XY':
          drawXY(ctx, W, H, dpr, bufs.sample, bufs.sampleR, { accentPrimary, accentGlow, bgInset, gridStroke });
          break;
        case 'WT':
          drawWavetable(ctx, W, H, dpr, bufs.sample, tNow, { accentPrimary, bgInset, gridStroke });
          break;
      }

      raf = requestAnimationFrame(draw);
    };

    raf = requestAnimationFrame(draw);
    return () => {
      stopped = true;
      cancelAnimationFrame(raf);
    };
  }, [bufs, synthesise]);

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
  accentGlow: string;
  bgInset: string;
  gridStroke: string;
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

function drawScope(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  sample: Float32Array,
  c: ScopeColors,
) {
  // Decay trail: draw last frame at 20% before redrawing this one.
  // We accomplish that by painting a semi-transparent bg over the
  // previous frame instead of clearing — cheap, no extra canvas.
  ctx.fillStyle = c.bgInset;
  ctx.globalAlpha = 0.8; // leaves 20% of previous frame visible
  ctx.fillRect(0, 0, W, H);
  ctx.globalAlpha = 1;

  // Re-stamp the grid so it doesn't ghost out.
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

  // Trace.
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

interface SpectrumColors {
  accentPrimary: string;
  accentSecondary: string;
  bgInset: string;
  gridStroke: string;
}

function drawSpectrum(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  bufs: {
    sample: Float32Array;
    fftRe: Float32Array;
    fftIm: Float32Array;
    peaks: Float32Array;
  },
  c: SpectrumColors,
) {
  drawGrid(ctx, W, H, dpr, c.bgInset, c.gridStroke);

  // Hann window + FFT.
  const { sample, fftRe, fftIm, peaks } = bufs;
  for (let i = 0; i < SAMPLE_COUNT; i++) {
    const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (SAMPLE_COUNT - 1)));
    fftRe[i] = sample[i] * w;
    fftIm[i] = 0;
  }
  fftInPlace(fftRe, fftIm);

  // Log-spaced bands → magnitudes.
  const BANDS = peaks.length; // 96
  const halfN = SAMPLE_COUNT / 2;
  const minBin = 2;
  const maxBin = halfN - 1;
  const logMin = Math.log(minBin);
  const logMax = Math.log(maxBin);

  // Gradient: violet → magenta → white at top.
  const grad = ctx.createLinearGradient(0, H, 0, 0);
  grad.addColorStop(0, c.accentPrimary);
  grad.addColorStop(0.65, c.accentSecondary);
  grad.addColorStop(1, '#FFFFFF');

  const gap = 1 * dpr;
  const bandWidth = (W - gap * (BANDS + 1)) / BANDS;

  for (let b = 0; b < BANDS; b++) {
    const fLo = Math.exp(logMin + ((logMax - logMin) * b) / BANDS);
    const fHi = Math.exp(logMin + ((logMax - logMin) * (b + 1)) / BANDS);
    const lo = Math.max(minBin, Math.floor(fLo));
    const hi = Math.max(lo + 1, Math.ceil(fHi));
    let mag = 0;
    for (let k = lo; k < hi && k < halfN; k++) {
      const m = Math.hypot(fftRe[k], fftIm[k]);
      if (m > mag) mag = m;
    }
    // Map to dB-ish then to 0..1.
    const db = 20 * Math.log10(mag + 1e-6);
    const norm = Math.max(0, Math.min(1, (db + 30) / 50));

    // Peak hold — decays ~1s.
    const decay = 0.985;
    peaks[b] = Math.max(norm, peaks[b] * decay);

    const x = gap + b * (bandWidth + gap);
    const barH = norm * (H - 4 * dpr);
    ctx.fillStyle = grad;
    ctx.fillRect(x, H - barH, bandWidth, barH);

    // Peak line.
    const py = H - peaks[b] * (H - 4 * dpr);
    ctx.fillStyle = 'rgba(255,255,255,0.6)';
    ctx.fillRect(x, py - 1 * dpr, bandWidth, 1 * dpr);
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

  // Lissajous.
  ctx.shadowColor = c.accentGlow;
  ctx.shadowBlur = 4 * dpr;
  ctx.strokeStyle = c.accentPrimary;
  ctx.lineWidth = 1.5 * dpr;
  ctx.beginPath();
  const cx = W / 2;
  const cy = H / 2;
  const scale = Math.min(W, H) * 0.42;
  for (let i = 0; i < SAMPLE_COUNT; i++) {
    const x = cx + L[i] * scale;
    const y = cy - R[i] * scale;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
  ctx.shadowBlur = 0;
}

interface WTColors {
  accentPrimary: string;
  bgInset: string;
  gridStroke: string;
}

function drawWavetable(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  sample: Float32Array,
  tNow: number,
  c: WTColors,
) {
  drawGrid(ctx, W, H, dpr, c.bgInset, c.gridStroke);

  // 7 layered curves, animated z-depth shifting — "tunnel of frames".
  const LAYERS = 7;
  const cx = W / 2;
  const cy = H / 2;
  const cycleSamples = Math.floor(SAMPLE_RATE / FUNDAMENTAL_HZ); // ~200
  const useSamples = Math.min(cycleSamples, SAMPLE_COUNT);

  for (let l = LAYERS - 1; l >= 0; l--) {
    // Each layer offset in time (z-depth) and shrunk + shifted up the
    // canvas to imply perspective.
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
    for (let i = 0; i < useSamples; i++) {
      const x = xL + (i / (useSamples - 1)) * xw;
      const y = y0 - sample[i] * H * 0.22 * scale;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }
  ctx.globalAlpha = 1;
}
