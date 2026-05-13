import React, { useEffect, useState } from 'react';
import './BootSplash.css';

// Six waveform paths morphed during phase 2 (400-1300ms, swapped every 150ms).
// All share the same viewBox (0 0 400 80) and end-points (0,40) → (400,40)
// so the SVG line "bends" between them rather than translating.
const WAVEFORM_PATHS: string[] = [
  // 0: flat baseline (phase 1 entry)
  'M0 40 L400 40',
  // 1: sine
  'M0 40 Q50 0 100 40 T200 40 T300 40 T400 40',
  // 2: square
  'M0 40 L25 40 L25 10 L75 10 L75 70 L125 70 L125 10 L175 10 L175 70 L225 70 L225 10 L275 10 L275 70 L325 70 L325 10 L375 10 L375 40 L400 40',
  // 3: saw
  'M0 40 L50 10 L50 70 L100 10 L100 70 L150 10 L150 70 L200 10 L200 70 L250 10 L250 70 L300 10 L300 70 L350 10 L350 70 L400 40',
  // 4: triangle
  'M0 40 L50 10 L100 70 L150 10 L200 70 L250 10 L300 70 L350 10 L400 40',
  // 5: wavetable (asymmetric bell-stack)
  'M0 40 C40 40 60 5 100 20 S160 70 200 35 S280 10 320 55 S380 30 400 40',
  // 6: complex (interference / additive)
  'M0 40 C25 25 40 55 60 35 S100 15 130 45 S170 70 200 30 S240 10 270 50 S310 65 340 25 S380 45 400 40',
];

interface BootSplashProps {
  onDone?: () => void;
}

type Phase = 'line' | 'morph' | 'wordmark' | 'fade';

const WORDMARK = 'TIMBRE';
const CHAR_INTERVAL_MS = 40;

export const BootSplash: React.FC<BootSplashProps> = ({ onDone }) => {
  const [phase, setPhase] = useState<Phase>('line');
  const [pathIndex, setPathIndex] = useState(0);
  const [typedChars, setTypedChars] = useState(0);
  const [caretVisible, setCaretVisible] = useState(true);

  // Phase scheduling. Single useEffect so timers cleanly cancel on unmount.
  useEffect(() => {
    const timers: number[] = [];

    // Phase 1 (0-400ms): line fade-in is purely CSS.
    timers.push(window.setTimeout(() => setPhase('morph'), 400));

    // Phase 2 (400-1300ms): step through 6 distinct waveform paths every 150ms.
    // First swap at +400ms (index 1, sine) so the line "inhales" out of flat.
    for (let i = 1; i < WAVEFORM_PATHS.length; i++) {
      timers.push(
        window.setTimeout(() => setPathIndex(i), 400 + i * 150),
      );
    }

    // Phase 3 (1300-1700ms): wordmark types char-by-char (~40ms/char × 6 = 240ms),
    // then a 160ms caret blink to fill the phase.
    timers.push(window.setTimeout(() => setPhase('wordmark'), 1300));
    for (let i = 1; i <= WORDMARK.length; i++) {
      timers.push(
        window.setTimeout(() => setTypedChars(i), 1300 + i * CHAR_INTERVAL_MS),
      );
    }
    // Blink caret once after final char (200ms cycle).
    timers.push(
      window.setTimeout(() => setCaretVisible(false), 1300 + WORDMARK.length * CHAR_INTERVAL_MS + 100),
    );
    timers.push(
      window.setTimeout(() => setCaretVisible(true), 1300 + WORDMARK.length * CHAR_INTERVAL_MS + 200),
    );

    // Phase 4 (1700-1800ms): trigger fade-out class. onDone fires at 1800.
    timers.push(window.setTimeout(() => setPhase('fade'), 1700));
    timers.push(window.setTimeout(() => onDone?.(), 1800));

    return () => {
      timers.forEach((t) => window.clearTimeout(t));
    };
  }, [onDone]);

  return (
    <div
      className={`boot-splash boot-splash-${phase}`}
      role="presentation"
      aria-hidden="true"
    >
      <div className="boot-splash-stage">
        <svg
          className="boot-splash-wave"
          viewBox="0 0 400 80"
          preserveAspectRatio="none"
          aria-hidden="true"
        >
          <path d={WAVEFORM_PATHS[pathIndex]} />
        </svg>
        <div className="boot-splash-wordmark" aria-hidden={phase !== 'wordmark' && phase !== 'fade'}>
          <span className="boot-splash-text">{WORDMARK.slice(0, typedChars)}</span>
          <span className={`boot-splash-caret${caretVisible ? '' : ' boot-splash-caret-off'}`}>
            |
          </span>
        </div>
      </div>
    </div>
  );
};
