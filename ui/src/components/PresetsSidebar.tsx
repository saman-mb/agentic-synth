import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
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
  // Phase 13 — hover-to-audition. Fires 300ms after pointer-enter; the
  // preset is pushed ephemerally to the engine without history side
  // effects. On pointer-leave, onAuditionEnd reverts engine state. On
  // click, onAuditionCommit clears the revert latch so the normal
  // onLoadPreset path commits cleanly.
  onAuditionStart?: (params: PatchParams) => void;
  onAuditionEnd?: () => void;
  onAuditionCommit?: () => void;
}

export function PresetsSidebar({
  currentPatch,
  onLoadPreset,
  onAuditionStart,
  onAuditionEnd,
  onAuditionCommit,
}: PresetsSidebarProps) {
  const [search, setSearch] = useState('');
  const [activeTags, setActiveTags] = useState<Set<PresetTag>>(new Set());
  const [userPresets, setUserPresets] = useState<PresetEntry[]>(() => loadUserPresets());
  const [favorites, setFavorites] = useState<Set<string>>(() => new Set(loadFavorites()));
  const [showSaveDialog, setShowSaveDialog] = useState(false);
  const [saveName, setSaveName] = useState('');
  const [saveTagsRaw, setSaveTagsRaw] = useState('');
  const [activePresetId, setActivePresetId] = useState<string | null>(null);
  // Brief cyan underline confirmation sweep on the Save Patch button when a
  // patch has just been committed. Cleared after 1000ms (matches the
  // .confirm-sweep keyframe total). Keyed by an incrementing token so a
  // rapid second save re-triggers the animation.
  const [saveSweepToken, setSaveSweepToken] = useState<number>(0);

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

  // ── Hover audition (Phase 13) ────────────────────────────────────
  // 300ms after pointer-enter, ephemerally push the preset to the
  // engine. On pointer-leave, revert. On click, commit + clear the
  // revert latch so the normal load-with-history path takes over.
  const hoverTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const auditioningIdRef = useRef<string | null>(null);

  const clearHoverTimer = useCallback(() => {
    if (hoverTimerRef.current) {
      clearTimeout(hoverTimerRef.current);
      hoverTimerRef.current = null;
    }
  }, []);

  const handleRowEnter = useCallback(
    (preset: PresetEntry) => {
      if (!onAuditionStart) return;
      clearHoverTimer();
      hoverTimerRef.current = setTimeout(() => {
        const cloned = JSON.parse(JSON.stringify(preset.params)) as PatchParams;
        auditioningIdRef.current = preset.id;
        onAuditionStart(cloned);
      }, 300);
    },
    [onAuditionStart, clearHoverTimer],
  );

  const handleRowLeave = useCallback(() => {
    clearHoverTimer();
    if (auditioningIdRef.current && onAuditionEnd) {
      onAuditionEnd();
    }
    auditioningIdRef.current = null;
  }, [clearHoverTimer, onAuditionEnd]);

  // Unmount safety: cancel any pending timer + revert any in-flight
  // audition so the engine doesn't end up stuck on a preview state.
  useEffect(() => {
    return () => {
      clearHoverTimer();
      if (auditioningIdRef.current && onAuditionEnd) onAuditionEnd();
      auditioningIdRef.current = null;
    };
  }, [clearHoverTimer, onAuditionEnd]);

  const handleSelectPreset = useCallback(
    (preset: PresetEntry) => {
      // Confirm the audition: clear the revert latch so handleLoadPreset
      // can commit the new patch as the canonical base.
      clearHoverTimer();
      auditioningIdRef.current = null;
      if (onAuditionCommit) onAuditionCommit();

      setActivePresetId(preset.id);
      // Deep-clone so external edits to the patch don't mutate the catalogue entry.
      const cloned = JSON.parse(JSON.stringify(preset.params)) as PatchParams;
      onLoadPreset(cloned);
    },
    [onLoadPreset, onAuditionCommit, clearHoverTimer],
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
    // Fire the cyan confirmation sweep. Bump first so re-saves restart
    // the keyframe; clear after 1000ms (sweep + hold + fade).
    setSaveSweepToken((n) => n + 1);
    window.setTimeout(() => setSaveSweepToken(0), 1000);
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
              aria-label={`Filter by tag ${t}`}
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
              onPointerEnter={() => handleRowEnter(p)}
              onPointerLeave={handleRowLeave}
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
          // Cyan confirmation sweep underline runs on the Save button itself
          // when a save has just been committed. Keyed via the token so a
          // re-save restarts the animation by re-mounting the pseudo element.
          key={`save-${saveSweepToken}`}
          className={`presets-save${saveSweepToken > 0 ? ' confirm-sweep' : ''}`}
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
