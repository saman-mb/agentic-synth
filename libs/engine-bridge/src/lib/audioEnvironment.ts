// Cross-platform Web Audio startup.
//
// Absorbs the three things that differ per browser/OS and otherwise show
// up as "the demo is silent on my phone":
//  - iOS mutes Web Audio whenever the hardware Ring/Silent switch is on,
//    unless the page declares a 'playback' audio session first.
//  - An AudioContext starts suspended until a user gesture, and iOS
//    re-suspends it after phone calls, Siri, or backgrounding.
//  - Older WebKit exposes only the webkitAudioContext constructor.

type AudioContextCtor = new (options?: AudioContextOptions) => AudioContext;

interface AudioSessionLike {
  type: string;
}

interface AudioGlobals {
  AudioContext?: AudioContextCtor;
  webkitAudioContext?: AudioContextCtor;
}

// Gestures that count as user activation for the autoplay policy.
const RESUME_EVENTS = ['pointerdown', 'touchend', 'keydown'] as const;

export function getAudioContextCtor(): AudioContextCtor | null {
  const g = globalThis as unknown as AudioGlobals;
  return g.AudioContext ?? g.webkitAudioContext ?? null;
}

export function isWebAudioSupported(): boolean {
  return getAudioContextCtor() !== null;
}

// Must run before the AudioContext is created, or iOS keeps the session
// at its default 'auto' type and the Silent switch still mutes us.
export function prepareAudioSession(): void {
  if (typeof navigator === 'undefined') return;
  const nav = navigator as Navigator & { audioSession?: AudioSessionLike };
  try {
    if (nav.audioSession) nav.audioSession.type = 'playback';
  } catch {
    // Non-fatal: the session type is a hint, not a requirement.
  }
}

export function createAudioContext(options?: AudioContextOptions): AudioContext {
  const Ctor = getAudioContextCtor();
  if (!Ctor) throw new Error('Web Audio is not supported in this browser.');
  prepareAudioSession();
  return new Ctor(options);
}

// WebKit and Firefox leave the promise returned by resume() pending
// indefinitely when the page has not seen a user gesture yet, so awaiting
// it directly can hang a caller forever. Bounding the wait keeps start-up
// responsive; the keep-alive listeners below resume the context for real
// as soon as a gesture arrives.
const RESUME_TIMEOUT_MS = 1000;

export async function resumeAudioContext(
  ctx: AudioContext,
  timeoutMs: number = RESUME_TIMEOUT_MS,
): Promise<void> {
  if (ctx.state !== 'suspended') return;
  await Promise.race([
    ctx.resume().catch(() => undefined),
    new Promise<void>((resolve) => {
      globalThis.setTimeout(resolve, timeoutMs);
    }),
  ]);
}

// Re-resumes `ctx` after the events that silently suspend it. Returns a
// teardown function; callers must invoke it on dispose so a closed
// context does not keep listeners alive.
export function keepAudioContextRunning(ctx: AudioContext): () => void {
  const resume = (): void => {
    if (ctx.state === 'suspended') void ctx.resume().catch(() => undefined);
  };
  const onVisibility = (): void => {
    if (typeof document !== 'undefined' && document.visibilityState === 'visible') resume();
  };

  const target = globalThis as unknown as Partial<EventTarget>;
  if (typeof target.addEventListener === 'function') {
    for (const name of RESUME_EVENTS) {
      target.addEventListener(name, resume, { passive: true } as AddEventListenerOptions);
    }
  }
  if (typeof document !== 'undefined') {
    document.addEventListener('visibilitychange', onVisibility);
  }
  ctx.addEventListener('statechange', resume);

  return () => {
    if (typeof target.removeEventListener === 'function') {
      for (const name of RESUME_EVENTS) target.removeEventListener(name, resume);
    }
    if (typeof document !== 'undefined') {
      document.removeEventListener('visibilitychange', onVisibility);
    }
    ctx.removeEventListener('statechange', resume);
  };
}
