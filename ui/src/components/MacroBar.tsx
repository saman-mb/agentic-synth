import React, { useCallback, useState } from 'react';
import { Knob } from './Knob';
import { ModSourcesStrip, ModSourceDot } from './ModSourcesStrip';
import type { ModSourceId } from '../data/modulation';
import './MacroBar.css';

// ── MacroBar (Phase 6 — visual + rename) ─────────────────────────────
//
// Four large macro knobs sitting in a thin strip between TopBar and the
// app body. Each macro has a 0..1 value and a renamable label. Click the
// label to edit; blur commits, Esc cancels.
//
// TODO (Phase 8): macro values modulate assigned (param, depth, polarity)
// targets. The routing layer ships with the mod matrix — Phase 6 only
// lands the knobs + naming so the layout is right.

export interface MacroState {
  label: string;
  value: number;
}

export interface MacroBarProps {
  macros: MacroState[];
  onMacroChange: (index: number, value: number) => void;
  onMacroRename: (index: number, label: string) => void;
}

export function MacroBar({ macros, onMacroChange, onMacroRename }: MacroBarProps) {
  const [editingIndex, setEditingIndex] = useState<number | null>(null);
  const [draftLabel, setDraftLabel] = useState('');

  const beginEdit = useCallback((i: number, currentLabel: string) => {
    setEditingIndex(i);
    setDraftLabel(currentLabel);
  }, []);

  const commitEdit = useCallback(() => {
    if (editingIndex === null) return;
    const next = draftLabel.trim();
    if (next.length > 0) onMacroRename(editingIndex, next);
    setEditingIndex(null);
  }, [editingIndex, draftLabel, onMacroRename]);

  const cancelEdit = useCallback(() => {
    setEditingIndex(null);
  }, []);

  return (
    <div className="macro-bar" role="group" aria-label="Macro controls">
      <span className="macro-bar-heading">MACROS</span>
      {macros.map((m, i) => {
        const sourceId = `macro${i + 1}` as ModSourceId;
        return (
        <div className="macro-slot" key={i}>
          <Knob
            value={m.value}
            onChange={(v) => onMacroChange(i, v)}
            name=""
            size="lg"
            defaultValue={0}
            displayValue={`${Math.round(m.value * 100)}%`}
          />
          <div className="macro-drag-handle">
            <ModSourceDot id={sourceId} size="sm" />
          </div>
          {editingIndex === i ? (
            <input
              type="text"
              className="macro-label-input"
              autoFocus
              value={draftLabel}
              onChange={(e) => setDraftLabel(e.target.value)}
              onBlur={commitEdit}
              onKeyDown={(e) => {
                if (e.key === 'Enter') commitEdit();
                else if (e.key === 'Escape') cancelEdit();
              }}
            />
          ) : (
            <button
              type="button"
              className="macro-label"
              onClick={() => beginEdit(i, m.label)}
              title="Rename macro"
            >
              {m.label}
            </button>
          )}
        </div>
        );
      })}
      <ModSourcesStrip
        sources={['lfo1', 'lfo2', 'env1', 'env2', 'velocity', 'keytrack']}
      />
    </div>
  );
}
