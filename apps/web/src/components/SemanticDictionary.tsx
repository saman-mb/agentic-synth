import React, { useCallback, useEffect, useState } from 'react';
import './SemanticDictionary.css';

// Delta fields shown in the UI. Float fields only — enum fields (osc type etc.)
// are preserved during round-trips but not yet exposed as editable controls.
const DELTA_FLOAT_FIELDS = [
  'filter_cutoff', 'filter_resonance', 'filter_drive', 'filter_env_mod',
  'amp_attack', 'amp_decay', 'amp_sustain', 'amp_release',
  'flt_attack', 'flt_decay', 'flt_sustain', 'flt_release',
  'lfo0_rate', 'lfo0_depth',
  'reverb_size', 'reverb_damping', 'reverb_width', 'reverb_mix',
  'delay_time', 'delay_feedback', 'delay_mix',
  'master_gain', 'portamento',
  'osc0_volume', 'osc0_semitone', 'osc0_detune',
  'osc0_fm_ratio', 'osc0_fm_depth', 'osc0_pulse_width',
  'osc1_volume', 'osc1_detune',
] as const;

const CONTEXTS = ['Generic', 'Bass', 'Pad', 'Lead', 'Keys', 'Percussion', 'Arp', 'Texture'] as const;

type Context = typeof CONTEXTS[number];

interface DeltaFields {
  [key: string]: number | string | boolean | undefined;
}

interface DictionaryEntry {
  keyword: string;
  context: Context;
  readonly: boolean;
  delta: DeltaFields;
}

interface Props {
  sendMessage: (msg: string) => void;
  lastMessage: string | null;
}

const emptyEntry = (): Omit<DictionaryEntry, 'readonly'> => ({
  keyword: '',
  context: 'Generic',
  delta: {},
});

export function SemanticDictionary({ sendMessage, lastMessage }: Props) {
  const [entries, setEntries] = useState<DictionaryEntry[]>([]);
  const [search, setSearch] = useState('');
  const [expandedIdx, setExpandedIdx] = useState<number | null>(null);
  const [newEntry, setNewEntry] = useState(emptyEntry);
  const [addingNew, setAddingNew] = useState(false);
  const [saving, setSaving] = useState(false);
  const [status, setStatus] = useState('');

  // Fetch on mount and on reconnect.
  useEffect(() => {
    sendMessage(JSON.stringify({ type: 'get_dictionary' }));
  }, [sendMessage]);

  useEffect(() => {
    if (!lastMessage) return;
    try {
      const msg = JSON.parse(lastMessage) as Record<string, unknown>;
      if (msg.type === 'dictionary_data' && Array.isArray(msg.entries)) {
        setEntries(msg.entries as DictionaryEntry[]);
        setSaving(false);
      }
    } catch { /* ignore non-dictionary frames */ }
  }, [lastMessage]);

  const refresh = useCallback(() => {
    sendMessage(JSON.stringify({ type: 'get_dictionary' }));
  }, [sendMessage]);

  const saveCustom = useCallback(() => {
    const customEntries = entries.filter((e) => !e.readonly);
    setSaving(true);
    setStatus('Saving…');
    sendMessage(JSON.stringify({ type: 'save_dictionary', entries: customEntries }));
    setTimeout(() => {
      setStatus('Saved.');
      setTimeout(() => setStatus(''), 2000);
    }, 300);
  }, [entries, sendMessage]);

  const addEntry = useCallback(() => {
    if (!newEntry.keyword.trim()) return;
    const e: DictionaryEntry = { ...newEntry, keyword: newEntry.keyword.trim(), readonly: false };
    setEntries((prev) => [...prev, e]);
    setNewEntry(emptyEntry());
    setAddingNew(false);
    setStatus('Entry added — click Save Custom to persist.');
  }, [newEntry]);

  const updateCustomDelta = useCallback(
    (idx: number, field: string, raw: string) => {
      const val = parseFloat(raw);
      if (isNaN(val)) return;
      setEntries((prev) =>
        prev.map((e, i) =>
          i === idx && !e.readonly
            ? { ...e, delta: { ...e.delta, [field]: val } }
            : e,
        ),
      );
    },
    [],
  );

  const filtered = entries.filter(
    (e) =>
      e.keyword.toLowerCase().includes(search.toLowerCase()) ||
      e.context.toLowerCase().includes(search.toLowerCase()),
  );

  const customCount = entries.filter((e) => !e.readonly).length;

  return (
    <div className="sem-dict">
      <div className="sem-dict-header">
        <span className="sem-dict-title">Semantic Dictionary</span>
        <span className="sem-dict-count">{entries.length} entries · {customCount} custom</span>
      </div>

      <div className="sem-dict-toolbar">
        <input
          className="sem-dict-search"
          placeholder="Search keyword or context…"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        <button className="sem-btn" onClick={refresh}>↺</button>
        <button className="sem-btn sem-btn-primary" onClick={saveCustom} disabled={saving || customCount === 0}>
          Save Custom
        </button>
      </div>

      {status && <div className="sem-dict-status">{status}</div>}

      <div className="sem-dict-list">
        {filtered.map((entry, idx) => {
          const realIdx = entries.indexOf(entry);
          const isOpen = expandedIdx === realIdx;
          const deltaKeys = Object.keys(entry.delta).filter((k) =>
            DELTA_FLOAT_FIELDS.includes(k as never),
          );

          return (
            <div key={`${entry.keyword}-${entry.context}-${realIdx}`} className={`sem-entry${entry.readonly ? '' : ' sem-entry-custom'}`}>
              <button
                className="sem-entry-header"
                onClick={() => setExpandedIdx(isOpen ? null : realIdx)}
              >
                <span className="sem-entry-keyword">{entry.keyword}</span>
                <span className={`sem-ctx-badge sem-ctx-${entry.context.toLowerCase()}`}>
                  {entry.context}
                </span>
                {!entry.readonly && <span className="sem-badge-custom">custom</span>}
                <span className="sem-entry-preview">
                  {deltaKeys.slice(0, 3).join(', ')}{deltaKeys.length > 3 ? '…' : ''}
                </span>
                <span className="sem-entry-chevron">{isOpen ? '▲' : '▼'}</span>
              </button>

              {isOpen && (
                <div className="sem-entry-body">
                  {DELTA_FLOAT_FIELDS.map((field) => {
                    const val = entry.delta[field] as number | undefined;
                    if (val === undefined && entry.readonly) return null;
                    return (
                      <div key={field} className="sem-field-row">
                        <label className="sem-field-label">{field}</label>
                        {entry.readonly ? (
                          <span className="sem-field-val">
                            {val !== undefined ? val.toFixed(4) : '—'}
                          </span>
                        ) : (
                          <input
                            className="sem-field-input"
                            type="number"
                            step="0.01"
                            defaultValue={val !== undefined ? val : ''}
                            placeholder="unset"
                            onBlur={(e) => updateCustomDelta(realIdx, field, e.target.value)}
                          />
                        )}
                      </div>
                    );
                  })}
                </div>
              )}
            </div>
          );
        })}
      </div>

      {addingNew ? (
        <div className="sem-add-panel">
          <div className="sem-add-row">
            <input
              className="sem-field-input sem-add-kw"
              placeholder="keyword"
              value={newEntry.keyword}
              onChange={(e) => setNewEntry((p) => ({ ...p, keyword: e.target.value }))}
            />
            <select
              className="sem-field-input"
              value={newEntry.context}
              onChange={(e) => setNewEntry((p) => ({ ...p, context: e.target.value as Context }))}
            >
              {CONTEXTS.map((c) => <option key={c}>{c}</option>)}
            </select>
          </div>
          <div className="sem-add-deltas">
            {(['filter_cutoff', 'reverb_mix', 'amp_attack', 'amp_sustain'] as const).map((f) => (
              <div key={f} className="sem-field-row">
                <label className="sem-field-label">{f}</label>
                <input
                  className="sem-field-input"
                  type="number"
                  step="0.01"
                  placeholder="unset"
                  onBlur={(e) => {
                    const v = parseFloat(e.target.value);
                    if (!isNaN(v))
                      setNewEntry((p) => ({ ...p, delta: { ...p.delta, [f]: v } }));
                  }}
                />
              </div>
            ))}
          </div>
          <div className="sem-add-actions">
            <button className="sem-btn sem-btn-primary" onClick={addEntry}>Add</button>
            <button className="sem-btn" onClick={() => { setAddingNew(false); setNewEntry(emptyEntry()); }}>
              Cancel
            </button>
          </div>
        </div>
      ) : (
        <button className="sem-btn sem-add-trigger" onClick={() => setAddingNew(true)}>
          + Add Custom Entry
        </button>
      )}
    </div>
  );
}
