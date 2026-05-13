import React, { useCallback, useEffect, useMemo, useState } from 'react';
import './PresetsSidebar.css';
import { makeDefaultPatch, PatchParams } from './KnobGrid';
import {
  loadFavorites,
  loadUserPresets,
  makeUserPresetId,
  PresetEntry,
  PRESET_TAGS,
  PresetTag,
  saveFavorites,
  saveUserPresets,
  STARTER_PRESETS,
} from '../data/presets';

// ── PresetsSidebar (Phase 6 — wired) ─────────────────────────────────
//
// Search + tag filter + click-to-load + favourites (★) + Save Patch +
// Init Patch. User presets persist via localStorage; favourites are an
// independent localStorage key so favouriting a factory preset doesn't
// require copying its params.
//
// Loading a preset is delegated up to App.tsx (`onLoadPreset`) — the
// sidebar doesn't know about patch history or the audio bridge. The
// callback hands a full PatchParams snapshot; App diffs against the
// current patch and forwards changed params to the C++ engine.

export interface PresetsSidebarProps {
  currentPatch: PatchParams;
  onLoadPreset: (params: PatchParams) => void;
}

export function PresetsSidebar({ currentPatch, onLoadPreset }: PresetsSidebarProps) {
  const [search, setSearch] = useState('');
  const [activeTags, setActiveTags] = useState<Set<PresetTag>>(new Set());
  const [userPresets, setUserPresets] = useState<PresetEntry[]>(() => loadUserPresets());
  const [favorites, setFavorites] = useState<Set<string>>(() => new Set(loadFavorites()));
  const [showSaveDialog, setShowSaveDialog] = useState(false);
  const [saveName, setSaveName] = useState('');
  const [saveTagsRaw, setSaveTagsRaw] = useState('');
  const [activePresetId, setActivePresetId] = useState<string | null>(null);

  // Persist on change.
  useEffect(() => {
    saveUserPresets(userPresets);
  }, [userPresets]);

  useEffect(() => {
    saveFavorites(Array.from(favorites));
  }, [favorites]);

  // Combined catalogue: starter first, then user. Filter is applied to this list.
  const allPresets = useMemo<PresetEntry[]>(
    () => [...STARTER_PRESETS, ...userPresets],
    [userPresets],
  );

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return allPresets.filter((p) => {
      if (q && !p.name.toLowerCase().includes(q)) return false;
      if (activeTags.size > 0) {
        const hasAny = p.tags.some((t) => activeTags.has(t));
        if (!hasAny) return false;
      }
      return true;
    });
  }, [allPresets, search, activeTags]);

  const toggleTag = useCallback((t: PresetTag) => {
    setActiveTags((prev) => {
      const next = new Set(prev);
      if (next.has(t)) next.delete(t);
      else next.add(t);
      return next;
    });
  }, []);

  const toggleFavorite = useCallback((id: string, e: React.MouseEvent) => {
    e.stopPropagation();
    setFavorites((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }, []);

  const handleSelectPreset = useCallback(
    (preset: PresetEntry) => {
      setActivePresetId(preset.id);
      // Deep-clone so external edits to the patch don't mutate the catalogue entry.
      const cloned = JSON.parse(JSON.stringify(preset.params)) as PatchParams;
      onLoadPreset(cloned);
    },
    [onLoadPreset],
  );

  const handleInitPatch = useCallback(() => {
    setActivePresetId(null);
    onLoadPreset(makeDefaultPatch());
  }, [onLoadPreset]);

  const handleSaveClick = useCallback(() => {
    setSaveName('');
    setSaveTagsRaw('');
    setShowSaveDialog(true);
  }, []);

  const handleSaveCommit = useCallback(() => {
    const name = saveName.trim();
    if (!name) {
      setShowSaveDialog(false);
      return;
    }
    // Parse tags: comma-separated, only those in PRESET_TAGS pass.
    const tagList = saveTagsRaw
      .split(',')
      .map((t) => t.trim())
      .filter(Boolean)
      .map((t) => {
        const match = PRESET_TAGS.find((p) => p.toLowerCase() === t.toLowerCase());
        return match;
      })
      .filter((t): t is PresetTag => Boolean(t));

    const entry: PresetEntry = {
      id: makeUserPresetId(),
      name,
      tags: tagList,
      params: JSON.parse(JSON.stringify(currentPatch)) as PatchParams,
      builtIn: false,
    };
    setUserPresets((prev) => [...prev, entry]);
    setShowSaveDialog(false);
  }, [saveName, saveTagsRaw, currentPatch]);

  const handleSaveCancel = useCallback(() => {
    setShowSaveDialog(false);
  }, []);

  return (
    <aside className="presets-sidebar" aria-label="Preset browser">
      <div className="presets-search-row">
        <input
          type="search"
          className="presets-search"
          placeholder="Search presets…"
          aria-label="Search presets"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
      </div>

      <div className="presets-tags" role="group" aria-label="Preset tags">
        {PRESET_TAGS.map((t) => {
          const active = activeTags.has(t);
          return (
            <button
              key={t}
              type="button"
              className={`presets-tag${active ? ' presets-tag-active' : ''}`}
              aria-pressed={active}
              onClick={() => toggleTag(t)}
              title={`Filter by ${t}`}
            >
              {t}
            </button>
          );
        })}
      </div>

      <ul className="presets-list" role="listbox" aria-label="Presets">
        {filtered.length === 0 && (
          <li className="presets-empty">No presets match.</li>
        )}
        {filtered.map((p) => {
          const isActive = activePresetId === p.id;
          const isFav = favorites.has(p.id);
          return (
            <li
              key={p.id}
              role="option"
              aria-selected={isActive}
              className={`presets-item${isActive ? ' presets-item-active' : ''}`}
              onClick={() => handleSelectPreset(p)}
            >
              <button
                type="button"
                className={`presets-star${isFav ? ' presets-star-on' : ''}`}
                onClick={(e) => toggleFavorite(p.id, e)}
                aria-label={isFav ? `Unfavourite ${p.name}` : `Favourite ${p.name}`}
                title={isFav ? 'Unfavourite' : 'Favourite'}
              >
                {isFav ? '★' : '☆'}
              </button>
              <span className="presets-item-name">{p.name}</span>
              <span className="presets-item-tag">
                {p.tags[0] ?? (p.builtIn ? 'Init' : 'User')}
              </span>
            </li>
          );
        })}
      </ul>

      <div className="presets-footer">
        <button
          type="button"
          className="presets-init"
          onClick={handleInitPatch}
          title="Reset to default patch"
        >
          Init Patch
        </button>
        <button
          type="button"
          className="presets-save"
          onClick={handleSaveClick}
          title="Save current patch"
        >
          Save Patch
        </button>
      </div>

      {showSaveDialog && (
        <div className="presets-save-dialog" role="dialog" aria-label="Save patch">
          <label className="presets-save-field">
            <span>Name</span>
            <input
              type="text"
              autoFocus
              value={saveName}
              onChange={(e) => setSaveName(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') handleSaveCommit();
                else if (e.key === 'Escape') handleSaveCancel();
              }}
              placeholder="My Patch"
            />
          </label>
          <label className="presets-save-field">
            <span>Tags</span>
            <input
              type="text"
              value={saveTagsRaw}
              onChange={(e) => setSaveTagsRaw(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') handleSaveCommit();
                else if (e.key === 'Escape') handleSaveCancel();
              }}
              placeholder="Bass, Lead"
            />
          </label>
          <div className="presets-save-actions">
            <button type="button" onClick={handleSaveCancel}>
              Cancel
            </button>
            <button type="button" className="presets-save-confirm" onClick={handleSaveCommit}>
              Save
            </button>
          </div>
        </div>
      )}
    </aside>
  );
}
