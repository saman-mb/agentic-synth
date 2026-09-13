// Drives the real synth engine from a real page so the cross-browser test
// measures actual rendered audio rather than mocks. Dev-server only: it is
// not an input to the production build.
import { createSynthEngine, type SynthEngine } from '@agentic-synth/engine-bridge';

interface HarnessResult {
  kind: string | null;
  peak: number;
  sampleCount: number;
  error?: string;
}

declare global {
  interface Window {
    __HARNESS_RESULT__?: HarnessResult;
  }
}

// Polled rather than sampled once: an AudioContext can still be finishing
// its resume when ensureStarted() resolves, so a single fixed-delay read
// races start-up and reports silence that never actually happened.
const AUDIBLE_TIMEOUT_MS = 3000;
const POLL_INTERVAL_MS = 50;
const AUDIBLE_THRESHOLD = 0.01;
const SCOPE_SAMPLES = 1024;

function peakOf(samples: number[]): number {
  let peak = 0;
  for (const s of samples) peak = Math.max(peak, Math.abs(s));
  return peak;
}

async function run(engine: SynthEngine): Promise<HarnessResult> {
  await engine.ensureStarted();
  engine.noteOn(60, 110);

  const deadline = Date.now() + AUDIBLE_TIMEOUT_MS;
  let peak = 0;
  let sampleCount = 0;
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, POLL_INTERVAL_MS));
    const samples = engine.getScopeSamples(SCOPE_SAMPLES);
    sampleCount = samples.length;
    peak = Math.max(peak, peakOf(samples));
    if (peak > AUDIBLE_THRESHOLD) break;
  }

  engine.noteOff(60);
  return {
    kind: (engine as { kind?: string | null }).kind ?? null,
    peak,
    sampleCount,
  };
}

const engine = createSynthEngine();
const button = document.getElementById('start');
const out = document.getElementById('out');

button?.addEventListener('click', () => {
  void run(engine)
    .then((result) => {
      window.__HARNESS_RESULT__ = result;
    })
    .catch((err: unknown) => {
      window.__HARNESS_RESULT__ = {
        kind: null,
        peak: 0,
        sampleCount: 0,
        error: err instanceof Error ? err.message : String(err),
      };
    })
    .finally(() => {
      if (out) out.textContent = JSON.stringify(window.__HARNESS_RESULT__);
    });
});
