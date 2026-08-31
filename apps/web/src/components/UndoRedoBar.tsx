import './UndoRedoBar.css';

// ─── Undo/Redo toolbar ───────────────────────────────────────────────────────
//
// Tiny stateless toolbar wired to usePatchHistory in App.tsx. Sits at the top
// of the knob panel. Keyboard shortcut listener for Cmd/Ctrl+Z and
// Cmd/Ctrl+Shift+Z lives in App.tsx (same pattern as AuditionKeyboard's
// global keys) so this component stays a pure presentational button group.

interface UndoRedoBarProps {
  onUndo: () => void;
  onRedo: () => void;
  canUndo: boolean;
  canRedo: boolean;
  cursor: number;
  historyLength: number;
}

export function UndoRedoBar({
  onUndo,
  onRedo,
  canUndo,
  canRedo,
  cursor,
  historyLength,
}: UndoRedoBarProps) {
  return (
    <div className="undo-redo-bar" role="group" aria-label="History">
      <button
        type="button"
        className="undo-redo-btn"
        onClick={onUndo}
        disabled={!canUndo}
        aria-label="Undo last patch change"
        title="Undo (Cmd/Ctrl+Z)"
      >
        Undo
      </button>
      <button
        type="button"
        className="undo-redo-btn"
        onClick={onRedo}
        disabled={!canRedo}
        aria-label="Redo"
        title="Redo (Cmd/Ctrl+Shift+Z)"
      >
        Redo
      </button>
      <span className="history-count" aria-live="polite">
        {cursor + 1} / {historyLength}
      </span>
    </div>
  );
}
