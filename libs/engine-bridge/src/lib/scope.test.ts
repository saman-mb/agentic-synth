import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
  SPECTRUM_FLOOR_DB,
  SPECTRUM_FFT_SIZE,
  computeSpectrumBands,
  dbToNorm,
  deinterleave,
  detectFundamental,
  fftInPlace,
  frequencyToX,
  lissajousPoint,
  parseScopeFrame,
} from './scope.ts';

const SR = 48000;
/** #434: on-screen band peak vs independent offline bin peak must agree within this. */
const OFFLINE_FFT_TOLERANCE_DB = 1.5;

function sine(amplitude: number, hz: number, n: number = SPECTRUM_FFT_SIZE, sampleRate = SR): Float32Array {
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = amplitude * Math.sin((2 * Math.PI * hz * i) / sampleRate);
  return out;
}

/**
 * Independent Hann-windowed single-sided peak in dBFS — same normalisation as
 * computeSpectrumBands but without log-band aggregation. Used to prove the
 * on-screen path agrees with an offline FFT of the same render (#434).
 */
function offlinePeakDb(samples: Float32Array): number {
  const n = SPECTRUM_FFT_SIZE;
  const re = new Float32Array(n);
  const im = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (n - 1)));
    re[i] = (i < samples.length ? samples[i] : 0) * w;
  }
  fftInPlace(re, im);
  const scale = 2 / (n * 0.5); // Hann coherent gain
  let peakAmp = 0;
  for (let k = 1; k < n / 2; k++) {
    const amp = scale * Math.sqrt(re[k] * re[k] + im[k] * im[k]);
    if (amp > peakAmp) peakAmp = amp;
  }
  return 20 * Math.log10(peakAmp + 1e-12);
}

// Deterministic uniform noise rescaled to a target RMS. Uniform on [-a, a]
// has RMS a/sqrt(3).
function noise(rms: number, n: number, seed = 12345): Float32Array {
  const out = new Float32Array(n);
  let state = seed >>> 0;
  const a = rms * Math.sqrt(3);
  for (let i = 0; i < n; i++) {
    state = (1664525 * state + 1013904223) >>> 0;
    out[i] = ((state / 0xffffffff) * 2 - 1) * a;
  }
  return out;
}

describe('spectrum scaling (#434)', () => {
  it('reads a full-scale sine at/near the 0 dBFS ceiling', () => {
    // 3000 Hz sits exactly on bin 64 of a 1024-point FFT at 48 kHz.
    const bands = computeSpectrumBands(sine(1.0, 3000), SR, 96);
    const peak = Math.max(...bands);
    assert.ok(peak > -3 && peak <= 0.001, `full-scale sine peak was ${peak} dBFS`);
  });

  it('maps the documented -90..0 dBFS window', () => {
    assert.equal(dbToNorm(0), 1);
    assert.equal(dbToNorm(-90), 0);
    assert.equal(dbToNorm(-45), 0.5);
    assert.equal(dbToNorm(-200), 0);
  });

  it('shows -50 dBFS RMS white noise as a clearly visible band', () => {
    const bands = computeSpectrumBands(noise(10 ** (-50 / 20), SPECTRUM_FFT_SIZE), SR, 96);
    const peak = Math.max(...bands);
    // A per-bin magnitude would read near the floor; band energy must land
    // well above it (the issue: the old scale hid audible noise).
    assert.ok(peak > SPECTRUM_FLOOR_DB + 15, `noise peak ${peak} dBFS is a sliver`);
    assert.ok(peak < -40, `noise peak ${peak} dBFS is implausibly loud`);
  });

  it('keeps silence at the floor', () => {
    const bands = computeSpectrumBands(new Float32Array(SPECTRUM_FFT_SIZE), SR, 96);
    for (const db of bands) assert.equal(db, SPECTRUM_FLOOR_DB);
  });

  it('places decade marks monotonically on the log axis', () => {
    const x100 = frequencyToX(100, SR, 800);
    const x1k = frequencyToX(1000, SR, 800);
    const x10k = frequencyToX(10000, SR, 800);
    assert.ok(x100 > 0 && x100 < x1k && x1k < x10k && x10k <= 800);
  });

  it('agrees with an independent offline FFT within stated tolerance', () => {
    // Same render: full-scale 3000 Hz sine (exact bin 64 @ 48 kHz / 1024).
    const render = sine(1.0, 3000);
    const onScreenPeak = Math.max(...computeSpectrumBands(render, SR, 96));
    const offlinePeak = offlinePeakDb(render);
    const delta = Math.abs(onScreenPeak - offlinePeak);
    assert.ok(
      delta <= OFFLINE_FFT_TOLERANCE_DB,
      `on-screen ${onScreenPeak} dBFS vs offline ${offlinePeak} dBFS (Δ=${delta}, tol=${OFFLINE_FFT_TOLERANCE_DB})`,
    );
  });
});

describe('stereo scope frames (#435)', () => {
  it('deinterleaves L/R and falls back to L when R is absent', () => {
    const { left, right } = deinterleave([1, 10, 2, 20, 3], 3);
    assert.deepEqual(Array.from(left), [1, 2, 3]);
    assert.deepEqual(Array.from(right), [10, 20, 3]);
  });

  it('parses the bridge object and the legacy flat array', () => {
    const obj = parseScopeFrame(
      { samples: [1, 2], sampleRate: 44100, droppedFrames: 7, staleWindows: 2, stale: true },
      48000,
    );
    assert.ok(obj);
    assert.equal(obj.sampleRate, 44100);
    assert.equal(obj.droppedFrames, 7);
    assert.equal(obj.stale, true);

    const arr = parseScopeFrame([1, 2, 3], 48000);
    assert.ok(arr);
    assert.equal(arr.sampleRate, 48000);
    assert.equal(arr.stale, false);
    assert.equal(parseScopeFrame('nope', 48000), null);
    assert.equal(parseScopeFrame(null, 48000), null);
  });
});

describe('wavetable pitch detection (#435)', () => {
  it('finds the actual fundamental instead of a hardcoded 220 Hz', () => {
    const a = detectFundamental(sine(0.8, 220), SR);
    assert.ok(Math.abs(a.hz - 220) / 220 < 0.03, `got ${a.hz} Hz`);
    assert.ok(a.confidence > 0.9);

    const b = detectFundamental(sine(0.8, 440), SR);
    assert.ok(Math.abs(b.hz - 440) / 440 < 0.03, `got ${b.hz} Hz`);
  });

  it('reports no pitch on silence', () => {
    const result = detectFundamental(new Float32Array(SPECTRUM_FFT_SIZE), SR);
    assert.equal(result.hz, 0);
  });
});

describe('XY geometry (#435)', () => {
  it('draws mono content as a 45-degree line', () => {
    const p = lissajousPoint(0.5, 0.5, 200, 100);
    const mono = lissajousPoint(0, 0, 200, 100);
    // Same magnitude on both axes, opposite canvas sign -> slope -1 (45 deg).
    assert.equal(p.x - mono.x, -(p.y - mono.y));
  });

  it('draws hard-left content along the horizontal axis', () => {
    const p = lissajousPoint(0.5, 0, 200, 100);
    assert.equal(p.y, 50);
  });
});
