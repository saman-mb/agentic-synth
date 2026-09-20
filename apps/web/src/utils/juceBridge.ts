interface JuceBackend {
  emitEvent: (name: string, payload: unknown) => void;
  addEventListener: (name: string, cb: (payload: unknown) => void) => number;
  removeEventListener?: (id: number) => void;
}

interface JuceGlobal {
  backend: JuceBackend;
  initialisationData?: { __juce__functions?: string[] };
}

export function getJuce(): JuceGlobal | null {
  const j = (window as unknown as { __JUCE__?: JuceGlobal }).__JUCE__;
  return j ?? null;
}

export const isJuceAvailable = (): boolean => getJuce() !== null;

interface PendingPromise {
  resolve: (value: unknown) => void;
  reject: (reason?: unknown) => void;
  timer: ReturnType<typeof setTimeout> | null;
}

const pendingPromises = new Map<number, PendingPromise>();
const PROMISE_ID_OFFSET = 1_000_000;
let nextPromiseId = PROMISE_ID_OFFSET;
let completeListenerWired = false;
const DEFAULT_TIMEOUT_MS = 10_000;

function ensureCompleteListener(juce: JuceGlobal): void {
  if (completeListenerWired) return;
  completeListenerWired = true;
  juce.backend.addEventListener('__juce__complete', (payload) => {
    const p = payload as { promiseId: number; result: unknown };
    if (typeof p.promiseId !== 'number' || p.promiseId < PROMISE_ID_OFFSET) return;
    const entry = pendingPromises.get(p.promiseId);
    if (entry) {
      if (entry.timer !== null) clearTimeout(entry.timer);
      pendingPromises.delete(p.promiseId);
      entry.resolve(p.result);
    }
  });
}

export function callNative(
  name: string,
  args: unknown[],
  timeoutMs: number = DEFAULT_TIMEOUT_MS
): Promise<unknown> {
  const juce = getJuce();
  if (!juce) return Promise.reject(new Error('JUCE backend not present'));
  ensureCompleteListener(juce);
  const id = nextPromiseId++;
  return new Promise<unknown>((resolve, reject) => {
    let timer: ReturnType<typeof setTimeout> | null = null;
    if (timeoutMs > 0) {
      timer = setTimeout(() => {
        if (pendingPromises.delete(id)) {
          reject(new Error(`callNative timeout (${timeoutMs}ms): ${name}`));
        }
      }, timeoutMs);
    }
    pendingPromises.set(id, { resolve, reject, timer });
    juce.backend.emitEvent('__juce__invoke', { name, params: args, resultId: id });
  });
}
