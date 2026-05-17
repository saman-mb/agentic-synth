import { useCallback, useEffect, useState } from 'react';

import { fetchPresets } from '../hooks/useSynthBridge';
import type { PatchPreviewData, WireIncoming, WireOutgoing } from '../types/chat';
import './PresetList.css';

// Phase D commit-UX (#260) — "Saved sounds" panel.
//
// Lists the presets persisted by PresetStore (one JSON file under the
// app-data dir). Each row offers Play (load into the engine), Bounce (send
// a `bounce_patch` so the C++ side opens a save dialog and renders), and
// Delete. The panel refreshes when a fresh `preset_committed` event lands
// on the wire bus.

interface StoredPresetWire {
  name: string;
  prompt?: string;
  created_ms?: number;
  patch: PatchPreviewData;
}

interface PresetListProps {
  open: boolean;
  onClose: () => void;
  // From parent: re-uses the existing patch-load path so engine + history
  // + animations stay consistent.
  onPlay: (patch: PatchPreviewData) => void;
  // Wire frame send — used for bounce + delete + the silent refresh after.
  send: (msg: WireOutgoing) => void;
  // Synchronous wire bus subscribe so we react instantly to preset_committed.
  subscribe: (cb: (msg: WireIncoming) => void) => () => void;
}

function formatStamp(ms: number | undefined): string {
  if (!ms || !Number.isFinite(ms)) return '';
  const d = new Date(ms);
  const today = new Date();
  const sameDay = d.toDateString() === today.toDateString();
  if (sameDay) return d.toTimeString().slice(0, 5);
  return `${d.getMonth() + 1}/${d.getDate()} ${d.toTimeString().slice(0, 5)}`;
}

export function PresetList({ open, onClose, onPlay, send, subscribe }: PresetListProps) {
  const [presets, setPresets] = useState<StoredPresetWire[]>([]);
  const [loading, setLoading] = useState(false);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = (await fetchPresets()) as { presets?: StoredPresetWire[] } | undefined;
      const list = Array.isArray(result?.presets) ? (result?.presets ?? []) : [];
      // Newest first.
      list.sort((a, b) => (b.created_ms ?? 0) - (a.created_ms ?? 0));
      setPresets(list);
    } catch {
      setPresets([]);
    } finally {
      setLoading(false);
    }
  }, []);

  // Refresh on open + on every preset_committed.
  useEffect(() => {
    if (open) void refresh();
  }, [open, refresh]);

  useEffect(() => {
    const unsub = subscribe((msg) => {
      if (msg.type === 'preset_committed') {
        void refresh();
      }
    });
    return unsub;
  }, [refresh, subscribe]);

  const handleDelete = useCallback(
    (name: string) => {
      send({ type: 'delete_preset', name });
      // Optimistic local update; refresh on next interaction.
      setPresets((prev) => prev.filter((p) => p.name !== name));
    },
    [send],
  );

  const handleBounce = useCallback(
    (sp: StoredPresetWire) => {
      send({ type: 'bounce_patch', patch: sp.patch, suggestedName: sp.name });
    },
    [send],
  );

  if (!open) return null;

  return (
    <aside className="preset-list" role="dialog" aria-label="Saved sounds">
      <header className="preset-list-header">
        <span className="preset-list-title">Saved sounds</span>
        <button
          type="button"
          className="preset-list-close"
          onClick={onClose}
          aria-label="Close saved sounds"
        >
          ×
        </button>
      </header>
      <div className="preset-list-body">
        {loading && presets.length === 0 && (
          <p className="preset-list-empty">Loading…</p>
        )}
        {!loading && presets.length === 0 && (
          <p className="preset-list-empty">
            No saved sounds yet. Click <em>Keep this sound</em> on a patch you like.
          </p>
        )}
        {presets.map((sp) => (
          <article key={sp.name} className="preset-row">
            <header className="preset-row-head">
              <span className="preset-row-name">{sp.name}</span>
              <span className="preset-row-stamp">{formatStamp(sp.created_ms)}</span>
            </header>
            {sp.prompt && <p className="preset-row-prompt">{sp.prompt}</p>}
            <div className="preset-row-actions">
              <button
                type="button"
                className="preset-row-play"
                onClick={() => onPlay(sp.patch)}
                aria-label={`Play ${sp.name}`}
              >
                Play
              </button>
              <button
                type="button"
                className="preset-row-bounce"
                onClick={() => handleBounce(sp)}
                aria-label={`Bounce ${sp.name} to wav`}
              >
                Bounce to wav
              </button>
              <button
                type="button"
                className="preset-row-delete"
                onClick={() => handleDelete(sp.name)}
                aria-label={`Delete ${sp.name}`}
              >
                Delete
              </button>
            </div>
          </article>
        ))}
      </div>
    </aside>
  );
}
