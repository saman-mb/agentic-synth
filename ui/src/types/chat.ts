import type { PatchParams } from '../components/KnobGrid';
import type { ModSourceId } from '../data/modulation';

export type PatchPreviewData = PatchParams;

export interface AgentModRoute {
  target: string;
  amount: number;
}

export interface AgentModMacro {
  name?: string;
  label?: string;
  routes?: AgentModRoute[];
}

export interface AgentModExtra {
  source: ModSourceId | 'LFO1' | 'LFO2' | 'FilterEnv' | 'AmpEnv' | 'Velocity' | 'KeyTrack';
  target: string;
  amount: number;
}

export interface AgentModulationPlan {
  macros?: AgentModMacro[];
  extras?: AgentModExtra[];
}

export interface PatchVariation {
  label: 'A' | 'B';
  patch: PatchPreviewData;
  modulation?: AgentModulationPlan;
}

export type FeedbackKind = 'like' | 'dislike';

export interface ChatMessage {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  streaming?: boolean;
  patch?: PatchPreviewData;
  modulation?: AgentModulationPlan;
  variations?: PatchVariation[];
  feedback?: FeedbackKind;
  rationale?: string;
  // Phase 10 §16 — 'sudo make me a sound' easter egg renders as a
  // green-on-black terminal block with `terminal: true`. Lines are
  // shown one per row in a monospace CRT-styled card.
  terminal?: boolean;
  terminalLines?: string[];
}

// WebSocket wire protocol
export type WireIncoming =
  | { type: 'token'; content: string }
  | { type: 'patch'; variation: 'A' | 'B'; data: PatchPreviewData; modulation?: AgentModulationPlan }
  | { type: 'done' }
  | { type: 'error'; message: string }
  | { type: 'rationale'; text: string }
  | { type: 'suggest_variations'; variations: ProactiveSuggestion[] }
  | { type: 'patch_update'; patch?: PatchPreviewData; params?: Record<string, number>; modulation?: AgentModulationPlan }
  | { type: 'transcript'; text: string }
  // Two-step LLM flow — the ENHANCER stage emits the 9-section
  // plain-text sound-design brief that the patch generator then
  // receives instead of the raw prompt. Chat ticker swaps from
  // HEARING IT OUT to SHAPING on receipt; brief is also folded into
  // the assistant message's rationale on `done` so it survives in
  // the "Why this patch?" details after the generation completes.
  | { type: 'enhancement'; brief: string };

export interface ProactiveSuggestion {
  label: string;
  description: string;
  patch: PatchPreviewData;
  modulation?: AgentModulationPlan;
}

export type WireOutgoing =
  | { type: 'generate'; prompt: string; sessionId: string }
  | { type: 'feedback'; messageId: string; kind: FeedbackKind; patch?: PatchPreviewData }
  | { type: 'play_midi_note'; note: number; velocity: number; duration_ms: number }
  | { type: 'note_on'; note: number; velocity: number }
  | { type: 'note_off'; note: number };
