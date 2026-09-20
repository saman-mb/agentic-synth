// Pure scope/spectrum/XY math shared by the browser engines and the web
// Visualizer (#434/#435/#436).
//
// Lives in engine-bridge rather than apps/web because this package HAS a unit
// test target (`node --experimental-strip-types --test ...`); the web app's
// `test` target is a no-op. Every function here is deterministic and free of
// DOM/WebAudio, so it can be pinned by tests.

// ── Spectrum display window ──────────────────────────────────────────
//
// The magnitude spectrum is normalised so a full-scale sine reads 0 dBFS.
// The displayed window is -90..0 dBFS: -90 dB is 1/31623 amplitude, below the
// noise floor of any real render, and audibly-relevant content (e.g. white
// noise at -50 dBFS RMS) lands around -65 dBFS in a single log band — about
// 28% of the band height, clearly visible, not a sliver.
export const SPECTRUM_FLOOR_DB = -90;
export const SPECTRUM_CEIL_DB = 0;
export const SPECTRUM_FFT_SIZE = 1024;

// Hann window mean (coherent gain). A full-scale sine of amplitude A through a
// Hann-windowed N-point FFT produces a bin magnitude of A*N*G/2.
const HANN_COHERENT_GAIN = 0.5;

export interface ScopeFrame {
  /** Interleaved L/R samples: [L0, R0, L1, R1, ...]. */
  samples: number[];
  /** The engine's real playback sample rate, in Hz. */
  sampleRate: number;
  droppedFrames: number;
  staleWindows: number;
  /** True when the producer overran this window; the consumer must skip it. */
  stale: boolean;
}

export function emptyScopeFrame(sampleRate: number): ScopeFrame {
  return { samples: [], sampleRate, droppedFrames: 0, staleWindows: 0, stale: false };
}

/**
 * Accept either the bridge's `{ samples, sampleRate, ... }` object (current)
 * or a bare number array (older shim / tests) and normalise it. Returns null
 * when the payload is unusable.
 */
export function parseScopeFrame(result: unknown, sampleRateFallback: number): ScopeFrame | null {
  if (Array.isArray(result)) {
    return { samples: result as number[], sampleRate: sampleRateFallback, droppedFrames: 0, staleWindows: 0, stale: false };
  }
  if (result && typeof result === 'object') {
    const obj = result as Record<string, unknown>;
    const raw = obj.samples;
    if (!Array.isArray(raw)) return null;
    const sr = typeof obj.sampleRate === 'number' && obj.sampleRate > 0 ? obj.sampleRate : sampleRateFallback;
    return {
      samples: raw as number[],
      sampleRate: sr,
      droppedFrames: typeof obj.droppedFrames === 'number' ? obj.droppedFrames : 0,
      staleWindows: typeof obj.staleWindows === 'number' ? obj.staleWindows : 0,
      stale: obj.stale === true,
    };
  }
  return null;
}

/**
 * Split an interleaved [L0, R0, L1, R1, ...] buffer into channel arrays.
 * Missing R samples fall back to L so a mono source still draws honestly.
 */
export function deinterleave(
  interleaved: ArrayLike<number>,
  frames: number,
): { left: Float32Array; right: Float32Array } {
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const l = interleaved[i * 2];
    const r = interleaved[i * 2 + 1];
    left[i] = typeof l === 'number' ? l : 0;
    right[i] = typeof r === 'number' ? r : left[i];
  }
  return { left, right };
}

/** Radix-2 in-place FFT. Real input (imag pre-zeroed). */
export function fftInPlace(re: Float32Array, im: Float32Array): void {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i];
      re[i] = re[j];
      re[j] = tr;
      const ti = im[i];
      im[i] = im[j];
      im[j] = ti;
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

/**
 * Log-spaced band magnitudes in dBFS. Band b spans [exp(logMin + k*b), ...)
 * over FFT bins; each band's amplitude is the root of the summed bin power,
 * scaled to a single-sided amplitude spectrum for a Hann window. A full-scale
 * sine therefore lands at ~0 dBFS regardless of FFT size, and a broadband
 * noise floor shows the energy in its band rather than the (much lower)
 * per-bin magnitude. Result is clamped to [SPECTRUM_FLOOR_DB, SPECTRUM_CEIL_DB].
 */
export function computeSpectrumBands(
  samples: Float32Array,
  sampleRate: number,
  bandCount: number,
  fftSize: number = SPECTRUM_FFT_SIZE,
): Float32Array {
  void sampleRate; // band edges are bin-relative; kept for API symmetry/docs
  const n = fftSize;
  const re = new Float32Array(n);
  const im = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (n - 1)));
    re[i] = (i < samples.length ? samples[i] : 0) * w;
  }
  fftInPlace(re, im);

  const out = new Float32Array(bandCount);
  const halfN = n / 2;
  const minBin = 2;
  const maxBin = halfN - 1;
  const logMin = Math.log(minBin);
  const logMax = Math.log(maxBin);
  const scale = 2 / (n * HANN_COHERENT_GAIN);

  for (let b = 0; b < bandCount; b++) {
    const fLo = Math.exp(logMin + ((logMax - logMin) * b) / bandCount);
    const fHi = Math.exp(logMin + ((logMax - logMin) * (b + 1)) / bandCount);
    const lo = Math.max(minBin, Math.floor(fLo));
    const hi = Math.min(maxBin, Math.max(lo + 1, Math.ceil(fHi)));
    let power = 0;
    for (let k = lo; k <= hi && k < halfN; k++) power += re[k] * re[k] + im[k] * im[k];
    const amp = scale * Math.sqrt(power);
    const db = 20 * Math.log10(amp + 1e-12);
    out[b] = Math.max(SPECTRUM_FLOOR_DB, Math.min(SPECTRUM_CEIL_DB, db));
  }
  return out;
}

/** Map a dBFS value into 0..1 across the documented display window. */
export function dbToNorm(db: number): number {
  const span = SPECTRUM_CEIL_DB - SPECTRUM_FLOOR_DB;
  return Math.max(0, Math.min(1, (db - SPECTRUM_FLOOR_DB) / span));
}

/** x position (0..width) of a frequency on the log-spaced band axis. */
export function frequencyToX(hz: number, sampleRate: number, width: number, fftSize: number = SPECTRUM_FFT_SIZE): number {
  const minBin = 2;
  const maxBin = fftSize / 2 - 1;
  const bin = Math.max(minBin, Math.min(maxBin, (hz * fftSize) / sampleRate));
  return (width * (Math.log(bin) - Math.log(minBin))) / (Math.log(maxBin) - Math.log(minBin));
}

export interface Fundamental {
  hz: number;
  confidence: number;
}

/**
 * Autocorrelation fundamental estimate over a short time-domain window, used
 * to derive the wavetable cycle length from the actual playing note (never a
 * hardcoded 220 Hz). Mirrors the C++ PitchDetector's approach: normalised
 * autocorrelation with parabolic peak interpolation, confidence 0..1. Returns
 * hz = 0 when the window is silent or unpitched.
 */
export function detectFundamental(samples: Float32Array, sampleRate: number): Fundamental {
  const n = samples.length;
  if (n < 64 || sampleRate <= 0) return { hz: 0, confidence: 0 };

  let energy = 0;
  for (let i = 0; i < n; i++) energy += samples[i] * samples[i];
  const rms = Math.sqrt(energy / n);
  if (rms < 1e-4) return { hz: 0, confidence: 0 };

  const minLag = Math.max(2, Math.floor(sampleRate / 2000));
  const maxLag = Math.min(n - 2, Math.ceil(sampleRate / 50));
  if (maxLag <= minLag) return { hz: 0, confidence: 0 };

  let bestLag = -1;
  let bestVal = 0;
  const corr = new Float32Array(maxLag + 2);
  for (let lag = minLag; lag <= maxLag; lag++) {
    let num = 0;
    let denA = 0;
    let denB = 0;
    for (let i = 0; i + lag < n; i++) {
      const a = samples[i];
      const b = samples[i + lag];
      num += a * b;
      denA += a * a;
      denB += b * b;
    }
    const den = Math.sqrt(denA * denB);
    const r = den > 0 ? num / den : 0;
    corr[lag] = r;
    if (r > bestVal) {
      bestVal = r;
      bestLag = lag;
    }
  }
  if (bestLag < 0 || bestVal < 0.5) return { hz: 0, confidence: bestVal };

  // Parabolic interpolation around the peak for sub-sample lag accuracy.
  let lag = bestLag;
  const y0 = corr[bestLag - 1] ?? 0;
  const y1 = corr[bestLag];
  const y2 = corr[bestLag + 1] ?? 0;
  const denom = 2 * (2 * y1 - y0 - y2);
  if (Math.abs(denom) > 1e-9) lag = bestLag + (y2 - y0) / denom;

  return { hz: sampleRate / lag, confidence: bestVal };
}

/**
 * Canvas position of one L/R sample on the XY (Lissajous) axes. Mono content
 * (L == R) maps to a 45-degree line; hard-left content maps to the horizontal
 * axis. Shared with the renderer so the geometry is unit-testable.
 */
export function lissajousPoint(
  left: number,
  right: number,
  width: number,
  height: number,
): { x: number; y: number } {
  const cx = width / 2;
  const cy = height / 2;
  const scale = Math.min(width, height) * 0.42;
  return { x: cx + left * scale, y: cy - right * scale };
}
