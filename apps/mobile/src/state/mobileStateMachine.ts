import type { MobileSession, MobileState } from './mobileState';

const LEGAL_TRANSITIONS: Record<MobileState, MobileState[]> = {
  idle: ['say', 'error', 'hear'],
  say: ['hear', 'idle', 'error'],
  hear: ['shape', 'error', 'say'],
  shape: ['variations', 'keep', 'say', 'hear', 'error'],
  variations: ['shape', 'keep', 'hear', 'error'],
  keep: ['idle', 'shape', 'error'],
  error: ['idle'],
};

export function canTransition(from: MobileState, to: MobileState): boolean {
  return LEGAL_TRANSITIONS[from]?.includes(to) ?? false;
}

export function transitionSession(
  session: MobileSession,
  to: MobileState,
  opts?: { statusMessage?: string; returnState?: MobileState },
): MobileSession {
  if (!canTransition(session.state, to) && !(session.state === 'error' && opts?.returnState)) {
    if (session.state === 'error' && to === opts?.returnState) {
      return {
        ...session,
        state: to,
        returnState: undefined,
        statusMessage: opts?.statusMessage ?? session.statusMessage,
      };
    }
    throw new Error(`Illegal MobileState transition: ${session.state} → ${to}`);
  }

  if (session.state === 'error' && opts?.returnState && to !== 'idle') {
    return {
      ...session,
      state: to,
      returnState: undefined,
      statusMessage: opts.statusMessage ?? session.statusMessage,
    };
  }

  return {
    ...session,
    state: to,
    returnState: to === 'error' ? opts?.returnState ?? session.returnState : undefined,
    statusMessage: opts?.statusMessage ?? session.statusMessage,
  };
}

/** Boot path: cold start loads demo patch and enters hear (scaffold #316). */
export function bootToHear(session: MobileSession, statusMessage = 'Playing demo patch'): MobileSession {
  return transitionSession(
    { ...session, isPlaying: true },
    'hear',
    { statusMessage },
  );
}
