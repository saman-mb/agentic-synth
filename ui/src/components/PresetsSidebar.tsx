import React from 'react';
import './PresetsSidebar.css';

// ── PresetsSidebar (Phase 4 placeholder) ─────────────────────────────
//
// Static scaffolding for the preset browser. No real preset data is
// wired yet (Phase 6 owns: tag filtering, audition-on-hover, favourites,
// search, save). Today this is just visual layout: search input, tag
// pill rail, a short list, and a "Save Patch" CTA.

const TAGS = ['Bass', 'Lead', 'Pad', 'Pluck', 'Keys', 'FX'] as const;

const SAMPLE_PRESETS = [
  { name: 'Init Patch',     tag: 'Init' },
  { name: 'Lunar Sub',      tag: 'Bass' },
  { name: 'Glass Choir',    tag: 'Pad' },
  { name: 'Acid Daydream',  tag: 'Lead' },
  { name: 'Velvet Pluck',   tag: 'Pluck' },
  { name: 'Tape Keys',      tag: 'Keys' },
  { name: 'Coming soon…',   tag: 'FX' },
];

export function PresetsSidebar() {
  return (
    <aside className="presets-sidebar" aria-label="Preset browser">
      <div className="presets-search-row">
        <input
          type="search"
          className="presets-search"
          placeholder="Search presets…"
          aria-label="Search presets"
          disabled
        />
      </div>

      <div className="presets-tags" role="group" aria-label="Preset tags">
        {TAGS.map((t) => (
          <button
            key={t}
            type="button"
            className="presets-tag"
            disabled
            aria-pressed="false"
            title={`Filter by ${t} (coming soon)`}
          >
            {t}
          </button>
        ))}
      </div>

      <ul className="presets-list" role="listbox" aria-label="Presets">
        {SAMPLE_PRESETS.map((p, i) => (
          <li
            key={p.name + i}
            role="option"
            aria-selected={i === 0}
            className={`presets-item${i === 0 ? ' presets-item-active' : ''}`}
          >
            <span className="presets-item-name">{p.name}</span>
            <span className="presets-item-tag">{p.tag}</span>
          </li>
        ))}
      </ul>

      <div className="presets-footer">
        <button type="button" className="presets-save" disabled title="Save Patch (coming soon)">
          Save Patch
        </button>
      </div>
    </aside>
  );
}
