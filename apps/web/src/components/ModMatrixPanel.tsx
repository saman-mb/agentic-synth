import React, { useCallback, useMemo, useState } from 'react';
import {
  ModConnection,
  ModMatrix,
  ModSourceId,
  MOD_SOURCES,
  MOD_SOURCE_LABELS,
  MOD_SOURCE_VARS,
  DESTINATION_CATALOG,
  destinationLabel,
} from '../data/modulation';
import { ModConstellation } from './ModConstellation';
import './ModMatrixPanel.css';

// ── ModMatrixPanel (Phase 8) ────────────────────────────────────────
//
// Collapsible drawer at the bottom of RightColumn. Two views:
//   LIST           — table-style rows (src → dst, amount, enable, x)
//   CONSTELLATION  — SVG view per REBRAND.md §12 wow #2
//
// Both views are backed by the same ModMatrix from App.tsx. The
// constellation can click-through a connection to highlight it back
// in the list view (sets `highlightedId`, which both views render).

interface ModMatrixPanelProps {
  modMatrix: ModMatrix;
  onUpdateConnection: (id: string, patch: Partial<ModConnection>) => void;
  onDeleteConnection: (id: string) => void;
  onAddConnection: (source: ModSourceId, destination: string) => void;
}

type View = 'list' | 'constellation';

export function ModMatrixPanel({
  modMatrix,
  onUpdateConnection,
  onDeleteConnection,
  onAddConnection,
}: ModMatrixPanelProps) {
  const [open, setOpen] = useState(true);
  const [view, setView] = useState<View>('list');
  const [highlightedId, setHighlightedId] = useState<string | null>(null);

  const connections = modMatrix.connections;
  const count = connections.length;

  return (
    <section className={`rc-card mod-matrix-panel${open ? ' is-open' : ''}`}>
      <div className="mod-matrix-header">
        <button
          type="button"
          className="mod-matrix-toggle"
          aria-expanded={open}
          aria-controls="mod-matrix-body"
          onClick={() => setOpen((v) => !v)}
        >
          <span className="rc-card-chev" aria-hidden="true">{open ? '▾' : '▸'}</span>
          <span className="rc-card-title">Modulation Matrix</span>
          {count > 0 && <span className="mod-matrix-count">{count}</span>}
        </button>
        {open && (
          <div className="mod-matrix-viewswitch" role="tablist" aria-label="View mode">
            <button
              type="button"
              role="tab"
              aria-selected={view === 'list'}
              className={`mod-matrix-viewbtn${view === 'list' ? ' is-active' : ''}`}
              onClick={() => setView('list')}
            >
              List
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={view === 'constellation'}
              className={`mod-matrix-viewbtn${view === 'constellation' ? ' is-active' : ''}`}
              onClick={() => setView('constellation')}
            >
              Constellation
            </button>
          </div>
        )}
      </div>

      {/* Paper-fold collapse (Phase 9): kept mounted so the rotateX
          transform plays both ways. When open=false, .is-folded pins
          the .fold-target to rotateX(-92deg) with opacity 0. */}
      <div className={`fold-host${open ? '' : ' is-folded'}`} aria-hidden={!open}>
        <div
          id="mod-matrix-body"
          className="mod-matrix-body fold-target"
        >
          {view === 'list' ? (
            <ListView
              connections={connections}
              highlightedId={highlightedId}
              onUpdateConnection={onUpdateConnection}
              onDeleteConnection={onDeleteConnection}
              onAddConnection={onAddConnection}
            />
          ) : (
            <ModConstellation
              connections={connections}
              highlightedId={highlightedId}
              onSelectConnection={(id) => {
                setHighlightedId(id);
                setView('list');
              }}
              onUpdateConnection={onUpdateConnection}
            />
          )}
        </div>
      </div>
    </section>
  );
}

// ─────────────────────────────────────────────────────────────────────
// List view
// ─────────────────────────────────────────────────────────────────────

interface ListViewProps {
  connections: ModConnection[];
  highlightedId: string | null;
  onUpdateConnection: (id: string, patch: Partial<ModConnection>) => void;
  onDeleteConnection: (id: string) => void;
  onAddConnection: (source: ModSourceId, destination: string) => void;
}

function ListView({
  connections,
  highlightedId,
  onUpdateConnection,
  onDeleteConnection,
  onAddConnection,
}: ListViewProps) {
  // "Add row" stages a source + destination locally; commit when both
  // are picked. Resets after commit so the user can chain adds.
  const [pendingSource, setPendingSource] = useState<ModSourceId>('lfo1');
  const [pendingDest, setPendingDest] = useState<string>(DESTINATION_CATALOG[0]);

  const commitAdd = useCallback(() => {
    onAddConnection(pendingSource, pendingDest);
  }, [pendingSource, pendingDest, onAddConnection]);

  if (connections.length === 0) {
    return (
      <div className="mod-matrix-empty">
        <p>
          Drag a mod source onto any knob to create a connection. Sources live in the
          bar above (LFOs, envs, macros, velocity, keytrack).
        </p>
        <AddRow
          pendingSource={pendingSource}
          pendingDest={pendingDest}
          onChangeSource={setPendingSource}
          onChangeDest={setPendingDest}
          onCommit={commitAdd}
        />
      </div>
    );
  }

  return (
    <div className="mod-matrix-list">
      <div className="mod-matrix-rowheader">
        <span>Source</span>
        <span />
        <span>Destination</span>
        <span>Amount</span>
        <span />
      </div>
      {connections.map((c) => (
        <ConnectionRow
          key={c.id}
          conn={c}
          highlighted={c.id === highlightedId}
          onUpdate={onUpdateConnection}
          onDelete={onDeleteConnection}
        />
      ))}
      <AddRow
        pendingSource={pendingSource}
        pendingDest={pendingDest}
        onChangeSource={setPendingSource}
        onChangeDest={setPendingDest}
        onCommit={commitAdd}
      />
    </div>
  );
}

interface ConnectionRowProps {
  conn: ModConnection;
  highlighted: boolean;
  onUpdate: (id: string, patch: Partial<ModConnection>) => void;
  onDelete: (id: string) => void;
}

function ConnectionRow({ conn, highlighted, onUpdate, onDelete }: ConnectionRowProps) {
  const cssVar = MOD_SOURCE_VARS[conn.source];
  const onAmount = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      const v = parseFloat(e.target.value);
      if (Number.isFinite(v)) onUpdate(conn.id, { amount: Math.max(-1, Math.min(1, v)) });
    },
    [conn.id, onUpdate],
  );

  const onContextMenu = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      onDelete(conn.id);
    },
    [conn.id, onDelete],
  );

  return (
    <div
      className={`mod-matrix-row${highlighted ? ' is-highlighted' : ''}${
        conn.enabled ? '' : ' is-disabled'
      }`}
      onContextMenu={onContextMenu}
    >
      <span
        className="mod-matrix-srcdot"
        aria-hidden="true"
        style={{ background: `var(${cssVar})` }}
      />
      <span className="mod-matrix-srclabel">{MOD_SOURCE_LABELS[conn.source]}</span>
      <span className="mod-matrix-arrow" aria-hidden="true">→</span>
      <span className="mod-matrix-dstlabel">{destinationLabel(conn.destination)}</span>
      <input
        type="range"
        className="mod-matrix-amount"
        min={-1}
        max={1}
        step={0.01}
        value={conn.amount}
        onChange={onAmount}
        aria-label={`Modulation amount for ${MOD_SOURCE_LABELS[conn.source]} to ${destinationLabel(conn.destination)}`}
        style={{ ['--mod-color' as string]: `var(${cssVar})` }}
      />
      <span className="mod-matrix-amount-readout">{conn.amount.toFixed(2)}</span>
      <button
        type="button"
        className={`mod-matrix-enable${conn.enabled ? ' is-on' : ''}`}
        aria-pressed={conn.enabled}
        aria-label={conn.enabled ? 'Disable connection' : 'Enable connection'}
        title={conn.enabled ? 'Disable' : 'Enable'}
        onClick={() => onUpdate(conn.id, { enabled: !conn.enabled })}
      >
        <span className="mod-matrix-enable-thumb" />
      </button>
      <button
        type="button"
        className="mod-matrix-delete"
        aria-label="Delete connection"
        title="Delete (or right-click row)"
        onClick={() => onDelete(conn.id)}
      >
        ×
      </button>
    </div>
  );
}

interface AddRowProps {
  pendingSource: ModSourceId;
  pendingDest: string;
  onChangeSource: (s: ModSourceId) => void;
  onChangeDest: (d: string) => void;
  onCommit: () => void;
}

function AddRow({
  pendingSource,
  pendingDest,
  onChangeSource,
  onChangeDest,
  onCommit,
}: AddRowProps) {
  const destOptions = useMemo(
    () => DESTINATION_CATALOG.map((k) => ({ value: k, label: destinationLabel(k) })),
    [],
  );
  // Cyan confirmation sweep on the "+ Add" button when a mod connection
  // has just been committed. Token-bumped so consecutive adds re-trigger.
  const [addSweepToken, setAddSweepToken] = useState<number>(0);
  const handleAddClick = useCallback(() => {
    onCommit();
    setAddSweepToken((n) => n + 1);
    window.setTimeout(() => setAddSweepToken(0), 1000);
  }, [onCommit]);
  return (
    <div className="mod-matrix-addrow">
      <select
        className="mod-matrix-select"
        value={pendingSource}
        onChange={(e) => onChangeSource(e.target.value as ModSourceId)}
        aria-label="New connection source"
      >
        {MOD_SOURCES.map((s) => (
          <option key={s} value={s}>
            {MOD_SOURCE_LABELS[s]}
          </option>
        ))}
      </select>
      <span className="mod-matrix-arrow" aria-hidden="true">→</span>
      <select
        className="mod-matrix-select"
        value={pendingDest}
        onChange={(e) => onChangeDest(e.target.value)}
        aria-label="New connection destination"
      >
        {destOptions.map((d) => (
          <option key={d.value} value={d.value}>
            {d.label}
          </option>
        ))}
      </select>
      <button
        type="button"
        key={`add-${addSweepToken}`}
        className={`mod-matrix-addbtn${addSweepToken > 0 ? ' confirm-sweep' : ''}`}
        onClick={handleAddClick}
      >
        + Add
      </button>
    </div>
  );
}
