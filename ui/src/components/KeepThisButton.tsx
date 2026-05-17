import { useCallback, useState } from 'react';
import type { PatchPreviewData, WireOutgoing } from '../types/chat';

// ---------------------------------------------------------------------------
// KeepThisButton — Phase D commit-UX (#260)
// ---------------------------------------------------------------------------
//
// Brand voice: "Keep this sound". On click we collect a name (inline prompt
// auto-seeded from the originating user prompt + a per-session counter), then
// fire a `commit_preset` wire frame. The C++ side persists it via PresetStore
// and emits `preset_committed` — parent components listen on that event to
// flash the cyan-sweep ceremony and refresh any "Saved sounds" panel.
//
// Deliberately tiny. Not a modal. The inline confirm row replaces the button
// while open; Esc/blur abandons; Enter commits.

const COUNTER_KEY = 'timbre:keep-counter';

function nextCounter(): number {
  try {
    const raw = window.localStorage.getItem(COUNTER_KEY);
    const n = raw ? parseInt(raw, 10) : 0;
    const next = Number.isFinite(n) ? n + 1 : 1;
    window.localStorage.setItem(COUNTER_KEY, String(next));
    return next;
  } catch {
    return Math.floor(Math.random() * 1000) + 1;
  }
}

function suggestName(prompt: string | undefined): string {
  const fragment = (prompt ?? '').trim().split(/\s+/).slice(0, 4).join(' ');
  const base = fragment.length > 0 ? fragment : 'sound';
  return `${base} #${nextCounter()}`;
}

export interface KeepThisButtonProps {
  patch: PatchPreviewData;
  prompt?: string;
  send: (msg: WireOutgoing) => void;
  // Optional visual variant — 'inline' fits inside a tile's action row,
  // 'standalone' adds extra padding for the play surface.
  variant?: 'inline' | 'standalone';
}

export function KeepThisButton({ patch, prompt, send, variant = 'inline' }: KeepThisButtonProps) {
  const [editing, setEditing] = useState(false);
  const [name, setName] = useState('');

  const open = useCallback(() => {
    setName(suggestName(prompt));
    setEditing(true);
  }, [prompt]);

  const commit = useCallback(() => {
    const trimmed = name.trim();
    if (trimmed.length === 0) {
      setEditing(false);
      return;
    }
    send({ type: 'commit_preset', name: trimmed, prompt: prompt ?? '', patch });
    setEditing(false);
  }, [name, patch, prompt, send]);

  const cancel = useCallback(() => setEditing(false), []);

  if (editing) {
    return (
      <span className={`keep-this keep-this--editing keep-this--${variant}`} role="group">
        <input
          type="text"
          className="keep-this-input"
          autoFocus
          value={name}
          maxLength={64}
          onChange={(e) => setName(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              commit();
            } else if (e.key === 'Escape') {
              e.preventDefault();
              cancel();
            }
          }}
          onBlur={() => {
            // Defer so a click on the confirm button still registers before
            // we hide the row.
            setTimeout(cancel, 120);
          }}
          aria-label="Name this sound"
        />
        <button
          type="button"
          className="keep-this-confirm"
          onMouseDown={(e) => e.preventDefault()}
          onClick={commit}
          aria-label="Confirm keep"
        >
          Keep
        </button>
      </span>
    );
  }

  return (
    <button
      type="button"
      className={`keep-this keep-this--${variant}`}
      onClick={open}
      aria-label="Keep this sound"
    >
      Keep this sound
    </button>
  );
}

// Bounce-to-wav twin button. Companion to KeepThisButton on the same
// surface; one-click emits `bounce_patch` and the C++ side opens a save
// dialog. Caller listens for `bounce_complete` to surface the toast.
export interface BounceToWavButtonProps {
  patch?: PatchPreviewData;
  suggestedName?: string;
  send: (msg: WireOutgoing) => void;
  variant?: 'inline' | 'standalone';
}

export function BounceToWavButton({ patch, suggestedName, send, variant = 'inline' }: BounceToWavButtonProps) {
  const onClick = useCallback(() => {
    send({ type: 'bounce_patch', patch, suggestedName });
  }, [patch, send, suggestedName]);
  return (
    <button
      type="button"
      className={`bounce-wav keep-this--${variant}`}
      onClick={onClick}
      aria-label="Bounce to wav"
    >
      Bounce to wav
    </button>
  );
}
