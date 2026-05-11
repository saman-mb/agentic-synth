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

export function AuditionKeyboard({ sendRaw, ready }: AuditionKeyboardProps) {
  const [activeNotes, setActiveNotes] = useState<Set<number>>(new Set());
  // Track which keyboard keys are currently held to suppress autorepeat.
  const pressedKeysRef = useRef<Set<string>>(new Set());
  const containerRef = useRef<HTMLDivElement>(null);
  const [globalKeysOn, setGlobalKeysOn] = useState(false);

  const charToNote = useMemo(() => {
    const m = new Map<string, number>();
    for (const k of KEYS) m.set(k.keyChar, k.note);
    return m;
  }, []);

  const playNote = useCallback(
    (note: number, durationMs: number) => {
      if (!ready) return;
      setActiveNotes((prev) => {
        const next = new Set(prev);
        next.add(note);
        return next;
      });
      // Auto-clear the highlight after duration so visual state matches
      // the audio envelope when C++ schedules the note-off.
      window.setTimeout(() => {
        setActiveNotes((prev) => {
          if (!prev.has(note)) return prev;
          const next = new Set(prev);
          next.delete(note);
          return next;
        });
      }, durationMs);
      sendRaw(
        JSON.stringify({
          type: 'play_midi_note',
          note,
          velocity: VELOCITY,
          duration_ms: durationMs,
        }),
      );
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
      const note = charToNote.get(e.key.toLowerCase());
      if (note === undefined) return;
      if (pressedKeysRef.current.has(e.key.toLowerCase())) return;
      pressedKeysRef.current.add(e.key.toLowerCase());
      e.preventDefault();
      playNote(note, HOLD_DURATION_MS);
    },
    [charToNote, playNote],
  );

  const handleGlobalKeyUp = useCallback((e: KeyboardEvent) => {
    pressedKeysRef.current.delete(e.key.toLowerCase());
  }, []);

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
      const note = charToNote.get(e.key.toLowerCase());
      if (note === undefined) return;
      e.preventDefault();
      playNote(note, HOLD_DURATION_MS);
    },
    [charToNote, playNote],
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
        <span className="audition-hint">
          {globalKeysOn ? 'Type a w s e d f t g y h u j k' : 'Click keys or focus this strip and type'}
        </span>
      </div>

      <div
        ref={containerRef}
        className="audition-keyboard"
        role="group"
        aria-label="Audition keyboard, C3 to C4"
        tabIndex={0}
        onKeyDown={onKeyDownLocal}
      >
        {KEYS.map((k) => (
          <button
            key={k.note}
            type="button"
            className={[
              'piano-key',
              k.isBlack ? 'piano-key-black' : 'piano-key-white',
              activeNotes.has(k.note) ? 'piano-key-active' : '',
            ].join(' ').trim()}
            disabled={!ready}
            // Use onClick (not onMouseDown alone) so native <button>
            // keyboard activation via Space/Enter also fires the note.
            // Trade-off: ~50ms latency vs mousedown; acceptable for audition.
            onClick={() => playNote(k.note, CLICK_DURATION_MS)}
            aria-label={`Play ${k.label}`}
            aria-pressed={activeNotes.has(k.note)}
            title={`${k.label} (${k.keyChar.toUpperCase()})`}
          >
            <span className="piano-key-label">{k.label}</span>
            <span className="piano-key-binding">{k.keyChar.toUpperCase()}</span>
          </button>
        ))}
      </div>
    </div>
  );
}
