import {
  type ChatMessage,
  type MobileSession,
  type PatchCardData,
  nextMessageId,
} from './mobileState';

/** Add a user message to the session. */
export function addUserMessage(session: MobileSession, text: string): MobileSession {
  const msg: ChatMessage = {
    id: nextMessageId(),
    role: 'user',
    text,
    timestamp: Date.now(),
  };
  return {
    ...session,
    messages: [...session.messages, msg],
  };
}

/** Add an agent message (with optional Patch Card) to the session. */
export function addAgentMessage(
  session: MobileSession,
  text: string,
  patchCard?: PatchCardData,
): MobileSession {
  const msg: ChatMessage = {
    id: nextMessageId(),
    role: 'agent',
    text,
    patchCard,
    timestamp: Date.now(),
  };
  return {
    ...session,
    messages: [...session.messages, msg],
    activePatchCardId: patchCard ? msg.id : session.activePatchCardId,
    isGenerating: false,
  };
}

/** Add a system (error / status) message. */
export function addSystemMessage(session: MobileSession, text: string): MobileSession {
  const msg: ChatMessage = {
    id: nextMessageId(),
    role: 'system',
    text,
    timestamp: Date.now(),
  };
  return {
    ...session,
    messages: [...session.messages, msg],
    isGenerating: false,
  };
}

/** Set generating state. */
export function setGenerating(session: MobileSession, generating: boolean): MobileSession {
  return { ...session, isGenerating: generating };
}

/** Switch view mode. */
export function setViewMode(session: MobileSession, viewMode: MobileSession['viewMode']): MobileSession {
  return { ...session, viewMode };
}

/** Update macro positions on the active Patch Card. */
export function updateActiveMacros(session: MobileSession, macros: number[]): MobileSession {
  if (!session.activePatchCardId) return session;
  return {
    ...session,
    messages: session.messages.map((m) =>
      m.id === session.activePatchCardId && m.patchCard
        ? { ...m, patchCard: { ...m.patchCard, macros } }
        : m,
    ),
  };
}

/** Activate a specific Patch Card by message ID. */
export function activatePatchCard(session: MobileSession, messageId: string): MobileSession {
  const msg = session.messages.find((m) => m.id === messageId);
  if (!msg?.patchCard) return session;
  return { ...session, activePatchCardId: messageId };
}
