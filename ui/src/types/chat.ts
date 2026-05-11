export interface PatchPreviewData {
  cutoffHz: number;
  resonance: number;
  attackS: number;
  sustainLevel: number;
  lfoDepth: number;
  reverbMix: number;
}

export interface PatchVariation {
  label: 'A' | 'B';
  patch: PatchPreviewData;
}

export type FeedbackKind = 'like' | 'dislike';

export interface ChatMessage {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  streaming?: boolean;
  patch?: PatchPreviewData;
  variations?: PatchVariation[];
  feedback?: FeedbackKind;
  rationale?: string;
}

// WebSocket wire protocol
export type WireIncoming =
  | { type: 'token'; content: string }
  | { type: 'patch'; variation: 'A' | 'B'; data: PatchPreviewData }
  | { type: 'done' }
  | { type: 'error'; message: string }
  | { type: 'rationale'; text: string }
  | { type: 'suggest_variations'; variations: ProactiveSuggestion[] };

export interface ProactiveSuggestion {
  label: string;
  description: string;
  patch: PatchPreviewData;
}

export type WireOutgoing =
  | { type: 'generate'; prompt: string; sessionId: string }
  | { type: 'feedback'; messageId: string; kind: FeedbackKind; patch?: PatchPreviewData }
  | { type: 'play_midi_note'; note: number; velocity: number; duration_ms: number };
