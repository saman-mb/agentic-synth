import { KeyboardEvent, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { PatchParams } from './KnobGrid';
import './PatchBrowser.css';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface BrowserEntry {
  /** Unique id, e.g. messageId from chat or a generated timestamp id. */
  id: string;
  /** Display label — "Dark warm pad" or first 40 chars of prompt, or auto. */
  label: string;
  /** Full patch state captured at generation time. */
  patch: PatchParams;
  /** Epoch ms — when this entry was captured. */
  timestamp: number;
  /** Persisted across reloads when true. */
  starred?: boolean;
}

export interface PatchBrowserProps {
  entries: BrowserEntry[];
  onSelect: (patch: PatchParams) => void;
  onStar: (id: string) => void;
  onRename?: (id: string, newLabel: string) => void;
  /** Clear all UNSTARRED entries. Starred entries are preserved. */
  onClear: () => void;
}

// ---------------------------------------------------------------------------
// Persistence (Starred-only)
// ---------------------------------------------------------------------------

export const PATCH_BROWSER_STORAGE_KEY = 'agentic-synth.patch-browser.v1';

/**
 * Versioned schema. Bump the storage key suffix when migrating.
 *
 * v1 schema:
 *   {
 *     version: 1,
 *     entries: BrowserEntry[]    // ONLY starred entries persist
 *   }
 */
interface PersistedV1 {
  version: 1;
  entries: BrowserEntry[];
}

export function loadStarred(): BrowserEntry[] {
  try {
    const raw = localStorage.getItem(PATCH_BROWSER_STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as PersistedV1;
    if (!parsed || parsed.version !== 1 || !Array.isArray(parsed.entries)) return [];
    // Validate minimally — drop anything malformed.
    return parsed.entries.filter(
      (e): e is BrowserEntry =>
        !!e &&
        typeof e.id === 'string' &&
        typeof e.label === 'string' &&
        typeof e.timestamp === 'number' &&
        !!e.patch &&
        typeof e.patch === 'object',
    );
  } catch {
    return [];
  }
}

export function saveStarred(entries: BrowserEntry[]) {
  try {
    const starredOnly = entries.filter((e) => e.starred);
    const payload: PersistedV1 = { version: 1, entries: starredOnly };
    localStorage.setItem(PATCH_BROWSER_STORAGE_KEY, JSON.stringify(payload));
  } catch {
    // ignore quota / disabled storage
  }
}

// ---------------------------------------------------------------------------
// Relative time formatter
// ---------------------------------------------------------------------------

function formatRelativeTime(then: number, now: number): string {
  const deltaSec = Math.max(0, Math.floor((now - then) / 1000));
  if (deltaSec < 5) return 'just now';
  if (deltaSec < 60) return `${deltaSec}s ago`;
  const min = Math.floor(deltaSec / 60);
  if (min < 60) return `${min} min ago`;
  const hr = Math.floor(min / 60);
  if (hr < 24) return `${hr} hr ago`;
  const day = Math.floor(hr / 24);
  return `${day}d ago`;
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

type Filter = 'all' | 'starred';

export function PatchBrowser({
  entries,
  onSelect,
  onStar,
  onRename,
  onClear,
}: PatchBrowserProps) {
  const [filter, setFilter] = useState<Filter>('all');
  const [renamingId, setRenamingId] = useState<string | null>(null);
  const [renameDraft, setRenameDraft] = useState('');
  const renameRef = useRef<HTMLInputElement | null>(null);

  // Refresh relative times every 30s without re-rendering for every tick.
  const [nowTick, setNowTick] = useState(() => Date.now());
  useEffect(() => {
    const id = window.setInterval(() => setNowTick(Date.now()), 30_000);
    return () => window.clearInterval(id);
  }, []);

  useEffect(() => {
    if (renamingId && renameRef.current) {
      renameRef.current.focus();
      renameRef.current.select();
    }
  }, [renamingId]);

  const visible = useMemo(() => {
    const base = filter === 'starred' ? entries.filter((e) => e.starred) : entries;
    // Newest first.
    return [...base].sort((a, b) => b.timestamp - a.timestamp);
  }, [entries, filter]);

  const commitRename = useCallback(
    (id: string) => {
      const trimmed = renameDraft.trim();
      if (trimmed && onRename) onRename(id, trimmed);
      setRenamingId(null);
      setRenameDraft('');
    },
    [onRename, renameDraft],
  );

  const cancelRename = useCallback(() => {
    setRenamingId(null);
    setRenameDraft('');
  }, []);

  const onRenameKey = useCallback(
    (e: KeyboardEvent<HTMLInputElement>, id: string) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        commitRename(id);
      } else if (e.key === 'Escape') {
        e.preventDefault();
        cancelRename();
      }
    },
    [commitRename, cancelRename],
  );

  const startRename = useCallback(
    (entry: BrowserEntry) => {
      if (!onRename) return;
      setRenamingId(entry.id);
      setRenameDraft(entry.label);
    },
    [onRename],
  );

  return (
    <div className="patch-browser" aria-label="Patch history browser">
      <div className="patch-browser-toolbar">
        <div
          className="patch-browser-filter"
          role="radiogroup"
          aria-label="Filter patches"
        >
          <button
            type="button"
            role="radio"
            aria-checked={filter === 'all'}
            className={`pb-filter-btn${filter === 'all' ? ' pb-filter-active' : ''}`}
            onClick={() => setFilter('all')}
          >
            All
          </button>
          <button
            type="button"
            role="radio"
            aria-checked={filter === 'starred'}
            className={`pb-filter-btn${filter === 'starred' ? ' pb-filter-active' : ''}`}
            onClick={() => setFilter('starred')}
          >
            Starred
          </button>
        </div>
        <button
          type="button"
          className="pb-clear-btn"
          onClick={onClear}
          disabled={!entries.some((e) => !e.starred)}
          aria-label="Clear unstarred patches"
          title="Clear unstarred patches (starred entries are kept)"
        >
          Clear
        </button>
      </div>

      {visible.length === 0 ? (
        <div className="patch-browser-empty" role="status">
          {filter === 'starred'
            ? 'No starred patches yet — star a patch to keep it across reloads.'
            : 'No patches yet — generate one from chat to see it here.'}
        </div>
      ) : (
        <ul className="patch-browser-list" role="list">
          {visible.map((entry) => {
            const isRenaming = renamingId === entry.id;
            return (
              <li key={entry.id} className="patch-browser-row">
                <button
                  type="button"
                  className="pb-row-main"
                  onClick={() => onSelect(entry.patch)}
                  aria-label={`Recall patch ${entry.label}`}
                >
                  {isRenaming ? (
                    <input
                      ref={renameRef}
                      type="text"
                      className="pb-rename-input"
                      value={renameDraft}
                      maxLength={80}
                      onChange={(e) => setRenameDraft(e.target.value)}
                      onKeyDown={(e) => onRenameKey(e, entry.id)}
                      onBlur={() => commitRename(entry.id)}
                      onClick={(e) => e.stopPropagation()}
                      aria-label="Rename patch"
                    />
                  ) : (
                    <span
                      className="pb-row-label"
                      onDoubleClick={(e) => {
                        e.stopPropagation();
                        startRename(entry);
                      }}
                      title={onRename ? 'Double-click to rename' : undefined}
                    >
                      {entry.label}
                    </span>
                  )}
                  <span className="pb-row-time" aria-hidden="true">
                    {formatRelativeTime(entry.timestamp, nowTick)}
                  </span>
                </button>
                <button
                  type="button"
                  className={`pb-star-btn${entry.starred ? ' pb-star-on' : ''}`}
                  aria-pressed={!!entry.starred}
                  aria-label={entry.starred ? 'Unstar patch' : 'Star patch'}
                  onClick={() => onStar(entry.id)}
                >
                  {entry.starred ? '\u2605' : '\u2606'}
                </button>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
