import React, { useState } from 'react';

interface DescriptorMapping {
  descriptor: string;
  targetParam: string;
  delta: number;
}

const defaultMappings: DescriptorMapping[] = [
  { descriptor: 'warm', targetParam: 'filterCutoffHz', delta: -0.3 },
  { descriptor: 'bright', targetParam: 'filterCutoffHz', delta: 0.5 },
  { descriptor: 'dark', targetParam: 'filterCutoffHz', delta: -0.5 },
  { descriptor: 'aggressive', targetParam: 'filterResonance', delta: 0.4 },
  { descriptor: 'soft', targetParam: 'ampAttackMs', delta: 0.3 },
  { descriptor: 'punchy', targetParam: 'ampAttackMs', delta: -0.2 },
  { descriptor: 'gritty', targetParam: 'filterResonance', delta: 0.3 },
  { descriptor: 'smooth', targetParam: 'filterCutoffHz', delta: -0.2 },
  { descriptor: 'hollow', targetParam: 'oscillatorMix', delta: 0.5 },
  { descriptor: 'boomy', targetParam: 'filterCutoffHz', delta: -0.4 },
];

export const SemanticDictionaryEditor: React.FC = () => {
  const [mappings, setMappings] = useState<DescriptorMapping[]>(defaultMappings);
  const [newDescriptor, setNewDescriptor] = useState('');
  const [newParam, setNewParam] = useState('filterCutoffHz');
  const [newDelta, setNewDelta] = useState('0.5');

  const addMapping = () => {
    if (!newDescriptor.trim()) return;
    setMappings(prev => [...prev, {
      descriptor: newDescriptor.trim().toLowerCase(),
      targetParam: newParam,
      delta: parseFloat(newDelta),
    }]);
    setNewDescriptor('');
  };

  const removeMapping = (index: number) => {
    setMappings(prev => prev.filter((_, i) => i !== index));
  };

  const exportMappings = () => {
    const blob = new Blob([JSON.stringify(mappings, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'descriptor_dataset_custom.json';
    a.click();
    URL.revokeObjectURL(url);
  };

  const paramOptions = [
    'filterCutoffHz', 'filterResonance', 'ampAttackMs', 'ampDecayMs',
    'ampSustainLevel', 'ampReleaseMs', 'lfoRateHz', 'lfoDepth',
    'oscillatorMix', 'pitchMod', 'filterEnvAmount',
  ];

  return (
    <div style={{ padding: 16, color: '#ccc', fontFamily: 'monospace', fontSize: 13 }}>
      <h3 style={{ color: '#4fc3f7', marginBottom: 12 }}>Semantic Dictionary</h3>

      <div style={{ maxHeight: 400, overflowY: 'auto', marginBottom: 12 }}>
        <table style={{ width: '100%', borderCollapse: 'collapse' }}>
          <thead>
            <tr style={{ color: '#999', borderBottom: '1px solid #333' }}>
              <th style={{ textAlign: 'left', padding: 4 }}>Descriptor</th>
              <th style={{ textAlign: 'left', padding: 4 }}>Parameter</th>
              <th style={{ textAlign: 'right', padding: 4 }}>Delta</th>
              <th style={{ width: 30 }} />
            </tr>
          </thead>
          <tbody>
            {mappings.map((m, i) => (
              <tr key={i} style={{ borderBottom: '1px solid #222' }}>
                <td style={{ padding: 4 }}>{m.descriptor}</td>
                <td style={{ padding: 4, color: '#888' }}>{m.targetParam}</td>
                <td style={{ padding: 4, textAlign: 'right', color: m.delta >= 0 ? '#4caf50' : '#f44336' }}>
                  {m.delta >= 0 ? '+' : ''}{m.delta.toFixed(2)}
                </td>
                <td>
                  <button onClick={() => removeMapping(i)}
                    style={{ background: 'none', border: 'none', color: '#666', cursor: 'pointer' }}>
                    ✕
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginBottom: 12 }}>
        <input
          value={newDescriptor}
          onChange={(e) => setNewDescriptor(e.target.value)}
          placeholder="descriptor"
          style={{ background: '#1a1a2e', border: '1px solid #333', color: '#fff', padding: '4px 8px', borderRadius: 4, width: 120 }}
        />
        <select
          value={newParam}
          onChange={(e) => setNewParam(e.target.value)}
          style={{ background: '#1a1a2e', border: '1px solid #333', color: '#fff', padding: '4px 8px', borderRadius: 4 }}
        >
          {paramOptions.map(p => <option key={p} value={p}>{p}</option>)}
        </select>
        <input
          value={newDelta}
          onChange={(e) => setNewDelta(e.target.value)}
          placeholder="delta"
          type="number"
          step={0.1}
          style={{ background: '#1a1a2e', border: '1px solid #333', color: '#fff', padding: '4px 8px', borderRadius: 4, width: 60 }}
        />
        <button onClick={addMapping}
          style={{ background: '#4fc3f7', border: 'none', color: '#000', padding: '4px 12px', borderRadius: 4, cursor: 'pointer' }}>
          Add
        </button>
      </div>

      <button onClick={exportMappings}
        style={{ background: '#333', border: 'none', color: '#ccc', padding: '6px 12px', borderRadius: 4, cursor: 'pointer' }}>
        Export to JSON
      </button>
    </div>
  );
};

export default SemanticDictionaryEditor;
