import type { PatchParams } from '@agentic-synth/shared-types';

/** v2 view mode — the app is either chatting-focused or playing-focused. */
export type ViewMode = 'chat' | 'play';

/** A single message in the conversation thread. */
export interface ChatMessage {
  id: string;
  role: 'user' | 'agent' | 'system';
  text: string;
  /** When role=agent and a patch was generated, this is the Patch Card data. */
  patchCard?: PatchCardData;
  timestamp: number;
}

/** Data for an inline Patch Card in the chat. */
export interface PatchCardData {
  patch: PatchParams;
  macros: number[];  // [brightness, movement, space, body] each 0–1
  prompt: string;
  brief: string;
}

/** The complete mobile session state. */
export interface MobileSession {
  viewMode: ViewMode;
  isPlaying: boolean;
  isGenerating: boolean;
  messages: ChatMessage[];
  activePatchCardId: string | null;
  statusMessage: string;
}

export const INITIAL_SESSION: MobileSession = {
  viewMode: 'chat',
  isPlaying: false,
  isGenerating: false,
  messages: [{
    id: 'welcome',
    role: 'agent',
    text: 'Describe a sound, or just tap to play.',
    timestamp: Date.now(),
  }],
  activePatchCardId: null,
  statusMessage: '',
};

let _nextId = 1;
export function nextMessageId(): string {
  return `msg-${Date.now()}-${_nextId++}`;
}
