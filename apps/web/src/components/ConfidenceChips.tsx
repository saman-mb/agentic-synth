// Phase 24 — Confidence chips.
//
// Surfaces what the agent likely parsed from the user's prompt before they
// hit send. Read-only mirror; the chip values are the user's exact words.
// Renders only when at least one category was detected (no apologetic
// empty state).
//
// Integration: parented inside .prompt-input-col below .prompt-input-wrap
// and above .prompt-input-hint in ChatInterface.tsx.

import { useEffect, useState } from 'react';

import { parseIntent, intentIsEmpty, type ParsedIntent } from './parseIntent';
import './ConfidenceChips.css';

interface ConfidenceChipsProps {
  prompt: string;
}

const DEBOUNCE_MS = 200;

export function useDeferredIntent(prompt: string, ms = DEBOUNCE_MS): ParsedIntent {
  const [intent, setIntent] = useState<ParsedIntent>(() => parseIntent(prompt));
  useEffect(() => {
    const t = window.setTimeout(() => setIntent(parseIntent(prompt)), ms);
    return () => window.clearTimeout(t);
  }, [prompt, ms]);
  return intent;
}

export function ConfidenceChips({ prompt }: ConfidenceChipsProps) {
  const intent = useDeferredIntent(prompt);
  if (intentIsEmpty(intent)) return null;

  return (
    <div className="confidence-chips" aria-label="Parsed intent preview">
      {intent.genre && <Chip cat="genre" val={intent.genre} />}
      {intent.role && <Chip cat="role" val={intent.role} />}
      {intent.character.map((c) => (
        <Chip key={`char-${c}`} cat="character" val={c} />
      ))}
    </div>
  );
}

function Chip({ cat, val }: { cat: string; val: string }) {
  return (
    <span className="confidence-chip" role="status">
      <span className="confidence-chip-cat">{cat}</span>
      <span className="confidence-chip-sep" aria-hidden="true">·</span>
      <span className="confidence-chip-val">{val}</span>
    </span>
  );
}
