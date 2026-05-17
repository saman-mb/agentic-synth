// Phase C onboarding (#256) — first-launch quick-start preset.
//
// Derived from the C++ ArchetypeLibrary's `cinematic_kubrick_pad` entry
// (src/mapper/ArchetypeLibrary.cpp). A faithful TypeScript transcription
// of those parameter values — duplicated here so the UI doesn't need a
// round-trip through C++ to seed the chat on first launch.
//
// The musical intent: a dark, cinematic, slowly-evolving pad that proves
// the synth makes a beautiful sound the moment the producer hits a key.
// No prompt required — the chat seeds it as an assistant message and the
// producer can audition immediately.
//
// Vocabulary register: musician-only ("warm cinematic pad"). Do NOT change
// the bubble copy to use "preset" / "module" / "config" — the onboarding
// brand voice (Phase 30 Brand Guardian) bans those words from the
// first-impression surface.

import type { PatchPreviewData } from '../types/chat';

export const QUICK_START_PATCH: PatchPreviewData = {
  osc: [
    {
      type: 2, // Sawtooth
      semitone_offset: -12,
      detune_cents: -11,
      wavetable_pos: 0,
      fm_ratio: 1,
      fm_depth: 0,
      volume: 0.75,
      pan: -0.6,
      pulse_width: 0.5,
      enabled: 1,
    },
    {
      type: 2, // Sawtooth
      semitone_offset: 0,
      detune_cents: 13,
      wavetable_pos: 0.3,
      fm_ratio: 1,
      fm_depth: 0,
      volume: 0.7,
      pan: 0.6,
      pulse_width: 0.5,
      enabled: 1,
    },
    {
      type: 6, // FM
      semitone_offset: -12,
      detune_cents: 0,
      wavetable_pos: 0,
      fm_ratio: 2.73,
      fm_depth: 0.35,
      volume: 0.4,
      pan: 0,
      pulse_width: 0.5,
      enabled: 1,
    },
  ],
  filter: {
    type: 0, // LowPass
    cutoff_hz: 1400,
    resonance: 0.35,
    env_mod: 0.4,
    key_track: 0,
    drive: 0.35,
  },
  filter_env: { attack_s: 1.8, decay_s: 5, sustain: 0.55, release_s: 5 },
  amp_env: { attack_s: 2.2, decay_s: 1.5, sustain: 0.85, release_s: 6 },
  lfo: [
    {
      waveform: 0, // Sine
      target: 2, // FilterCutoff
      rate_hz: 0.06,
      depth: 0.55,
      phase_offset: 0,
      bpm_sync: 0,
    },
    {
      waveform: 1, // Triangle
      target: 1, // Pitch
      rate_hz: 0.1,
      depth: 0.04,
      phase_offset: 0.25,
      bpm_sync: 0,
    },
  ],
  reverb: { size: 0.92, damping: 0.55, width: 1, mix: 0.42 },
  delay: { time_s: 0.5, feedback: 0.25, mix: 0.1, stereo: 0.6, bpm_sync: 0 },
  master_gain: 0.85,
  portamento_s: 0,
  voice_count: 8,
};

// localStorage key for the onboarding-completed flag. Centralised so the
// OnboardingOverlay component and the App's first-launch check agree on
// the exact string. Once set to '1', the overlay never reappears for the
// life of the install.
export const ONBOARDING_COMPLETED_KEY = 'timbre.onboarding.completed';

export function isOnboardingCompleted(): boolean {
  if (typeof window === 'undefined') return true;
  try {
    return window.localStorage.getItem(ONBOARDING_COMPLETED_KEY) === '1';
  } catch {
    return true; // pessimistic — never block a render on storage failure
  }
}

export function markOnboardingCompleted(): void {
  if (typeof window === 'undefined') return;
  try {
    window.localStorage.setItem(ONBOARDING_COMPLETED_KEY, '1');
  } catch {
    // ignore — private mode / storage disabled
  }
}
