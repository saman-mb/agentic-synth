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

// Sensory descriptors emitted by the morph loop (#249). Legacy 'A'/'B' kept
// alongside so existing 2-tile bubbles keep rendering; the morph loop hands
// us musician-register words ("brighter", "warmer", "wider", …) plus stable
// A–E fallbacks when no perceptual axis dominates.
export type PatchVariationLabel =
  | 'A' | 'B' | 'C' | 'D' | 'E'
  | 'brighter' | 'warmer'
  | 'longer' | 'snappier'
  | 'wider' | 'drier'
  | 'spread' | 'tighter'
  | string;

export interface PatchVariation {
  label: PatchVariationLabel;
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
  // Phase 26 — PatchAugmenter actions, one per auto-fix the C++ guardrail
  // applied to the LLM output. UI renders a small "patch adjusted" banner
  // under the bubble so users see what was repaired post-LLM and don't
  // blame the model for the layered result.
  augmenterActions?: string[];
  // Phase 10 §16 — 'sudo make me a sound' easter egg renders as a
  // green-on-black terminal block with `terminal: true`. Lines are
  // shown one per row in a monospace CRT-styled card.
  terminal?: boolean;
  terminalLines?: string[];
}

// WebSocket wire protocol
export type WireIncoming =
  | { type: 'token'; content: string }
  | { type: 'patch'; variation: 'A' | 'B'; data: PatchPreviewData; modulation?: AgentModulationPlan; augmenter_actions?: string[] }
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
  | { type: 'enhancement'; brief: string }
  // Phase B simple-view (#249/#263) — explicit "more variations" reply.
  // Distinct from `suggest_variations` (proactive 3-suggestion stream);
  // this fires once per `morph_request` user gesture and carries 5 tiles.
  | { type: 'variations_ready'; variations: PatchVariation[] }
  // Phase C failure-state UX (#269) — surfaced when the agent can't
  // produce a useful response. `kind` selects the banner copy; `detail`
  // is opaque engineering text the user can optionally expand to read.
  | {
      type: 'failure';
      kind: 'llm_offline' | 'prompt_unclear' | 'safety_block' | 'mic_denied';
      detail?: string;
    }
  // Phase D commit-UX (#260). Fires once per successful PresetStore.save
  // so the UI can re-fetch the saved-sounds list and play the ceremony.
  | { type: 'preset_committed'; name: string; prompt?: string; created_ms?: number; patch?: PatchPreviewData }
  // Phase D export-to-track (#268). Fires once per offline bounce. ok=false
  // covers both user cancel and write failure; UI distinguishes via `error`.
  | { type: 'bounce_complete'; ok: boolean; path?: string; error?: string }
  // Phase G / #247 — autocorrelation pitch detection over the PTT buffer.
  // Confidence-gated on the C++ side at >0.7. UI shows a "TIMBRE heard a
  // note around B♭3" chip near the prompt textarea.
  | { type: 'hum_pitch_detected'; midi_note: number; confidence: number; frequency_hz: number }
  // Phase G / #262 — MidiLearnStore captured a CC for the learning knob.
  // UI uses this to close the "Learn MIDI" affordance and reflect the
  // mapping (chip on the knob, value driven by physical controller).
  | { type: 'midi_learned'; knob_id: string; cc: number; channel: number };

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
  | { type: 'note_off'; note: number }
  // Phase B simple-view (#249) — request 5 fresh variations from the morph
  // loop. C++ replies asynchronously with `variations_ready`.
  | { type: 'morph_request' }
  // Phase D / #260 — persist the current patch as a named "saved sound".
  | { type: 'commit_preset'; name: string; prompt?: string; patch?: PatchPreviewData }
  // Phase D / #260 — fetch the saved-sounds list. Resolves with the JSON
  // returned by the native handler (not through subscribe events).
  | { type: 'get_presets' }
  // Phase D / #260 — drop a saved sound by name.
  | { type: 'delete_preset'; name: string }
  // Phase D / #268 — bounce the current patch (or a passed-in one) to a
  // .wav. JUCE opens a save-as FileChooser, then renders on a worker; the
  // UI listens for the `bounce_complete` event for the result.
  | { type: 'bounce_patch'; patch?: PatchPreviewData; suggestedName?: string }
  // Phase G / #262 — enter "Learn MIDI" mode for the given knob. Empty
  // knob_id cancels any in-flight learn. The C++ side captures the next
  // incoming non-bank-select CC and emits `midi_learned`.
  | { type: 'start_midi_learn'; knob_id: string }
  | { type: 'cancel_midi_learn' }
  | { type: 'clear_midi_mapping'; knob_id: string }
  | { type: 'get_midi_mappings' };
