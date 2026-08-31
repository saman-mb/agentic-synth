import {
  KeyboardEvent as ReactKeyboardEvent,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import './AuditionKeyboard.css';

// ── Audition keyboard ────────────────────────────────────────────────────────
//
// Lets the producer hear the current patch without a DAW: 13 keys C3..C4
// trigger play_midi_note frames over the JUCE native bridge. Velocity is
// hard-coded to 0.8 for Phase 6C; can be wired to a UI slider later.

const VELOCITY = 0.8;

// Click-to-play duration. The note-off is scheduled C++-side via
// play_midi_note(duration_ms), so we don't need to track mouse-up release
// for a guaranteed envelope close; mouse-up just sends an early note-off.
const CLICK_DURATION_MS = 350;
// Tap-and-fire duration for QWERTY + click. Short enough that releasing the
// key promptly leaves only a small audible tail, long enough to hear the
// patch character. There is no per-key noteOff today (would require a new
// wire path); the C++ side schedules the noteOff via duration_ms.
const HOLD_DURATION_MS = 500;

interface KeyDef {
  note: number;       // MIDI note number
  label: string;      // display label (e.g. "C3")
  isBlack: boolean;
  keyChar: string;    // QWERTY key (lowercase)
}

// 13 keys C3..C4 with QWERTY mapping per spec:
//   a w s e d f t g y h u j k → C3 C#3 D3 D#3 E3 F3 F#3 G3 G#3 A3 A#3 B3 C4
const KEYS: KeyDef[] = [
  { note: 48, label: 'C3',  isBlack: false, keyChar: 'a' },
  { note: 49, label: 'C#3', isBlack: true,  keyChar: 'w' },
  { note: 50, label: 'D3',  isBlack: false, keyChar: 's' },
  { note: 51, label: 'D#3', isBlack: true,  keyChar: 'e' },
  { note: 52, label: 'E3',  isBlack: false, keyChar: 'd' },
  { note: 53, label: 'F3',  isBlack: false, keyChar: 'f' },
  { note: 54, label: 'F#3', isBlack: true,  keyChar: 't' },
  { note: 55, label: 'G3',  isBlack: false, keyChar: 'g' },
  { note: 56, label: 'G#3', isBlack: true,  keyChar: 'y' },
  { note: 57, label: 'A3',  isBlack: false, keyChar: 'h' },
  { note: 58, label: 'A#3', isBlack: true,  keyChar: 'u' },
  { note: 59, label: 'B3',  isBlack: false, keyChar: 'j' },
  { note: 60, label: 'C4',  isBlack: false, keyChar: 'k' },
];

interface AuditionKeyboardProps {
  // Send a JSON-serialised wire frame over the bridge. Caller wires this
  // to useWebSocket().sendMessage (or useSynthBridge().sendRaw) so this
  // component stays bridge-agnostic.
  sendRaw: (frame: string) => void;
  // When false, disables sending and greys out the keys.
  ready: boolean;
}

const OCTAVE_KEY = 'timbre:audition-octave';
const MIN_OCTAVE_SHIFT = -3; // C0 floor (default C3 + (-3 * 12) = C0)
const MAX_OCTAVE_SHIFT = 5;  // C8 ceiling

export function AuditionKeyboard({ sendRaw, ready }: AuditionKeyboardProps) {
  const [activeNotes, setActiveNotes] = useState<Set<number>>(new Set());
  // Track which QWERTY keys are currently held → which MIDI note was actually
  // dispatched at keydown. Needed because the user can shift octaves between
  // keydown and keyup; without this the noteOff would carry a different MIDI
  // than the noteOn and the engine never matches → stuck note.
  const pressedKeysRef = useRef<Map<string, number>>(new Map());
  // Same idea for mouse/touch: map pointerId → MIDI dispatched at pointerdown.
  const pressedPointersRef = useRef<Map<number, number>>(new Map());
  const containerRef = useRef<HTMLDivElement>(null);
  const [globalKeysOn, setGlobalKeysOn] = useState(false);
  const [octaveShift, setOctaveShift] = useState<number>(() => {
    if (typeof window === 'undefined') return 0;
    const v = parseInt(window.localStorage.getItem(OCTAVE_KEY) ?? '0', 10);
    return Number.isFinite(v) ? Math.max(MIN_OCTAVE_SHIFT, Math.min(MAX_OCTAVE_SHIFT, v)) : 0;
  });
  const octaveShiftRef = useRef(octaveShift);
  useEffect(() => { octaveShiftRef.current = octaveShift; }, [octaveShift]);
  useEffect(() => {
    if (typeof window !== 'undefined') {
      window.localStorage.setItem(OCTAVE_KEY, String(octaveShift));
    }
  }, [octaveShift]);

  const transposeNote = useCallback((midi: number) => midi + octaveShiftRef.current * 12, []);

  const charToNote = useMemo(() => {
    const m = new Map<string, number>();
    for (const k of KEYS) m.set(k.keyChar, k.note);
    return m;
  }, []);

  const shiftOctave = useCallback((delta: number) => {
    setOctaveShift((cur) => Math.max(MIN_OCTAVE_SHIFT, Math.min(MAX_OCTAVE_SHIFT, cur + delta)));
  }, []);

  // One-shot note (fixed duration). Used by "Audition C3" button.
  const playNote = useCallback(
    (note: number, durationMs: number) => {
      if (!ready) return;
      setActiveNotes((prev) => {
        const next = new Set(prev);
        next.add(note);
        return next;
      });
      window.setTimeout(() => {
        setActiveNotes((prev) => {
          if (!prev.has(note)) return prev;
          const next = new Set(prev);
          next.delete(note);
          return next;
        });
      }, durationMs);
      sendRaw(
        JSON.stringify({ type: 'play_midi_note', note, velocity: VELOCITY, duration_ms: durationMs }),
      );
    },
    [ready, sendRaw],
  );

  // Expressive hold: separate note_on / note_off. Hold pointer/key = sustained.
  const noteOn = useCallback(
    (note: number) => {
      if (!ready) return;
      setActiveNotes((prev) => {
        if (prev.has(note)) return prev;
        const next = new Set(prev);
        next.add(note);
        return next;
      });
      sendRaw(JSON.stringify({ type: 'note_on', note, velocity: VELOCITY }));
    },
    [ready, sendRaw],
  );

  const noteOff = useCallback(
    (note: number) => {
      if (!ready) return;
      setActiveNotes((prev) => {
        if (!prev.has(note)) return prev;
        const next = new Set(prev);
        next.delete(note);
        return next;
      });
      sendRaw(JSON.stringify({ type: 'note_off', note }));
    },
    [ready, sendRaw],
  );

  // ── QWERTY mapping ─────────────────────────────────────────────────────────
  //
  // Two activation modes:
  //   1. Component-focused: native keydown on the container fires.
  //   2. Global toggle: when globalKeysOn === true we attach a window
  //      listener so the user can keep their hands on the keys while
  //      typing in the chat input would be awkward.
  //
  // Both paths filter out auto-repeat events (e.repeat === true) and
  // suppress propagation so we never double-trigger.

  const handleGlobalKey = useCallback(
    (e: KeyboardEvent) => {
      if (e.repeat) return;
      // Don't hijack modifier combos or input typing.
      if (e.metaKey || e.ctrlKey || e.altKey) return;
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
        return;
      }
      const lower = e.key.toLowerCase();
      // Octave shift hotkeys — z = down, x = up
      if (lower === 'z') { e.preventDefault(); shiftOctave(-1); return; }
      if (lower === 'x') { e.preventDefault(); shiftOctave(+1); return; }
      const note = charToNote.get(lower);
      if (note === undefined) return;
      if (pressedKeysRef.current.has(lower)) return;
      const midi = transposeNote(note);
      pressedKeysRef.current.set(lower, midi);
      e.preventDefault();
      noteOn(midi);
    },
    [charToNote, noteOn, shiftOctave, transposeNote],
  );

  const handleGlobalKeyUp = useCallback((e: KeyboardEvent) => {
    const k = e.key.toLowerCase();
    // Send the SAME midi we sent at keydown — not the current transposed
    // value. The user may have shifted octaves while the key was held; if
    // we recompute we'd noteOff a different note and the engine would
    // leave the original one ringing.
    const midi = pressedKeysRef.current.get(k);
    pressedKeysRef.current.delete(k);
    if (midi !== undefined) noteOff(midi);
  }, [noteOff]);

  useEffect(() => {
    if (!globalKeysOn) return;
    window.addEventListener('keydown', handleGlobalKey);
    window.addEventListener('keyup', handleGlobalKeyUp);
    return () => {
      window.removeEventListener('keydown', handleGlobalKey);
      window.removeEventListener('keyup', handleGlobalKeyUp);
    };
  }, [globalKeysOn, handleGlobalKey, handleGlobalKeyUp]);

  const onKeyDownLocal = useCallback(
    (e: ReactKeyboardEvent<HTMLDivElement>) => {
      if (e.repeat) return;
      const lower = e.key.toLowerCase();
      if (lower === 'z') { e.preventDefault(); shiftOctave(-1); return; }
      if (lower === 'x') { e.preventDefault(); shiftOctave(+1); return; }
      const note = charToNote.get(lower);
      if (note === undefined) return;
      if (pressedKeysRef.current.has(lower)) return;
      const midi = transposeNote(note);
      pressedKeysRef.current.set(lower, midi);
      e.preventDefault();
      noteOn(midi);
    },
    [charToNote, noteOn, shiftOctave, transposeNote],
  );

  const onKeyUpLocal = useCallback(
    (e: ReactKeyboardEvent<HTMLDivElement>) => {
      const k = e.key.toLowerCase();
      const midi = pressedKeysRef.current.get(k);
      pressedKeysRef.current.delete(k);
      if (midi !== undefined) noteOff(midi);
    },
    [noteOff],
  );

  return (
    <div className="audition-shell">
      <div className="audition-toolbar">
        <button
          type="button"
          className="audition-btn"
          onClick={() => playNote(48, 1000)}
          disabled={!ready}
          aria-label="Play C3 for one second"
          title="Play C3 (1 s)"
        >
          ▶ Audition C3
        </button>
        <label className="audition-toggle">
          <input
            type="checkbox"
            checked={globalKeysOn}
            onChange={(e) => setGlobalKeysOn(e.target.checked)}
            aria-label="Enable global QWERTY note input"
          />
          <span>QWERTY keys</span>
        </label>
        <div className="audition-octave" role="group" aria-label="Octave shift">
          <button
            type="button"
            className="audition-btn audition-octave-btn"
            onClick={() => shiftOctave(-1)}
            disabled={octaveShift <= MIN_OCTAVE_SHIFT}
            aria-label="Octave down (Z)"
            title="Octave down (Z)"
          >−</button>
          <span className="audition-octave-label" aria-live="polite">
            Oct {octaveShift >= 0 ? '+' : ''}{octaveShift}
          </span>
          <button
            type="button"
            className="audition-btn audition-octave-btn"
            onClick={() => shiftOctave(+1)}
            disabled={octaveShift >= MAX_OCTAVE_SHIFT}
            aria-label="Octave up (X)"
            title="Octave up (X)"
          >+</button>
        </div>
        <span className="audition-hint">
          {globalKeysOn ? 'a w s e d f t g y h u j k · Z/X octave' : 'Click or focus + type · Z/X octave'}
        </span>
      </div>

      <div
        ref={containerRef}
        className="audition-keyboard"
        role="group"
        aria-label="Audition keyboard, C3 to C4"
        tabIndex={0}
        onKeyDown={onKeyDownLocal}
        onKeyUp={onKeyUpLocal}
      >
        {KEYS.map((k) => {
          const midi = transposeNote(k.note);
          const labelOct = Math.floor(midi / 12) - 1; // MIDI 60 = C4
          const pitchClass = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][midi % 12];
          const displayLabel = `${pitchClass}${labelOct}`;
          return (
          <button
            key={k.note}
            type="button"
            className={[
              'piano-key',
              k.isBlack ? 'piano-key-black' : 'piano-key-white',
              activeNotes.has(midi) ? 'piano-key-active' : '',
            ].join(' ').trim()}
            disabled={!ready}
            // Expressive hold: pointerdown → noteOn, pointerup/leave → noteOff.
            // Hold the mouse / finger to sustain. Drag across keys: leaving a
            // key releases it, entering with button held triggers next note.
            // Capture the MIDI dispatched at pointerdown into pressedPointersRef
            // so we release the SAME note even if octave shifts mid-hold.
            onPointerDown={(e) => {
              e.preventDefault();
              (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
              pressedPointersRef.current.set(e.pointerId, midi);
              noteOn(midi);
            }}
            onPointerUp={(e) => {
              try { (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId); } catch {/* ignore */}
              const held = pressedPointersRef.current.get(e.pointerId);
              pressedPointersRef.current.delete(e.pointerId);
              if (held !== undefined) noteOff(held);
            }}
            onPointerEnter={(e) => {
              if (e.buttons === 1) {
                pressedPointersRef.current.set(e.pointerId, midi);
                noteOn(midi);
              }
            }}
            onPointerLeave={(e) => {
              if (e.buttons === 1) {
                const held = pressedPointersRef.current.get(e.pointerId);
                pressedPointersRef.current.delete(e.pointerId);
                if (held !== undefined) noteOff(held);
              }
            }}
            onPointerCancel={(e) => {
              const held = pressedPointersRef.current.get(e.pointerId);
              pressedPointersRef.current.delete(e.pointerId);
              if (held !== undefined) noteOff(held);
            }}
            aria-label={`Play ${displayLabel}`}
            aria-pressed={activeNotes.has(midi)}
            title={`${displayLabel} (${k.keyChar.toUpperCase()})`}
          >
            <span className="piano-key-label">{displayLabel}</span>
            <span className="piano-key-binding">{k.keyChar.toUpperCase()}</span>
          </button>
          );
        })}
      </div>
    </div>
  );
}
