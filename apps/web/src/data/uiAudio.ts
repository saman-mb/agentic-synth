// ── UI Audio (Phase 10 §17) ─────────────────────────────────────────
//
// TIMBRE is "mostly silent" — only two opt-in confirmation sounds:
//   • voice-transcribe pip   (40ms sine at A4, ~−30dBFS)
//   • patch-load tape-stop   (~200ms sine ramp 220Hz → 40Hz)
//
// Both are synthesised in a shared, lazily-created AudioContext. We
// keep the context out of React render paths so first playback is
// cheap and there's no churn on rerenders.

let sharedCtx: AudioContext | null = null;

function getCtx(): AudioContext | null {
  if (typeof window === 'undefined') return null;
  if (sharedCtx) return sharedCtx;
  const Ctor: typeof AudioContext | undefined =
    (window as unknown as { AudioContext?: typeof AudioContext }).AudioContext ??
    (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
  if (!Ctor) return null;
  try {
    sharedCtx = new Ctor();
  } catch {
    return null;
  }
  return sharedCtx;
}

// Some browsers create the context in 'suspended' state until a user
// gesture nudges it open. We try to resume on every play — cheap and
// idempotent — so the first pip after the first click reliably fires.
function ensureRunning(ctx: AudioContext): void {
  if (ctx.state === 'suspended') {
    void ctx.resume().catch(() => undefined);
  }
}

// ── Voice transcribe pip (40ms sine, A4 = 440Hz, −30dBFS) ──────────
export function playVoicePip(): void {
  const ctx = getCtx();
  if (!ctx) return;
  ensureRunning(ctx);
  const now = ctx.currentTime;
  const osc = ctx.createOscillator();
  osc.type = 'sine';
  osc.frequency.setValueAtTime(440, now);
  const gain = ctx.createGain();
  // −30dBFS ≈ 0.0316 peak. Tiny attack + decay to avoid click on edges.
  const peak = 0.0316;
  gain.gain.setValueAtTime(0, now);
  gain.gain.linearRampToValueAtTime(peak, now + 0.005);
  gain.gain.linearRampToValueAtTime(0, now + 0.04);
  osc.connect(gain).connect(ctx.destination);
  osc.start(now);
  osc.stop(now + 0.05);
}

// ── Patch-load "tape stop" thunk (200ms sine, 220Hz → 40Hz) ────────
export function playTapeStop(): void {
  const ctx = getCtx();
  if (!ctx) return;
  ensureRunning(ctx);
  const now = ctx.currentTime;
  const osc = ctx.createOscillator();
  osc.type = 'sine';
  osc.frequency.setValueAtTime(220, now);
  // exponentialRampToValueAtTime requires a strictly positive target.
  osc.frequency.exponentialRampToValueAtTime(40, now + 0.2);
  const gain = ctx.createGain();
  // Soft thunk — slightly louder than the pip, still quiet.
  const peak = 0.06;
  gain.gain.setValueAtTime(0, now);
  gain.gain.linearRampToValueAtTime(peak, now + 0.01);
  gain.gain.exponentialRampToValueAtTime(0.0001, now + 0.21);
  osc.connect(gain).connect(ctx.destination);
  osc.start(now);
  osc.stop(now + 0.22);
}
