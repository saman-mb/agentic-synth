import type { PatchPreviewData } from '../types/chat';
import './PatchPreview.css';

interface Props {
  patch: PatchPreviewData;
  label?: string;
}

interface BarProps {
  label: string;
  value: number; // 0..1
  displayValue: string;
}

function ParamBar({ label, value, displayValue }: BarProps) {
  return (
    <div className="param-bar">
      <span className="param-label">{label}</span>
      <div className="param-track">
        <div className="param-fill" style={{ width: `${value * 100}%` }} />
      </div>
      <span className="param-value">{displayValue}</span>
    </div>
  );
}

function normCutoff(hz: number): number {
  const lo = Math.log(20);
  const hi = Math.log(20000);
  return (Math.log(Math.max(20, hz)) - lo) / (hi - lo);
}

export function PatchPreview({ patch, label }: Props) {
  const lfoDepth = Math.max(...patch.lfo.map((l) => l.depth));
  return (
    <div className="patch-preview" aria-label={label ?? 'Patch preview'}>
      {label && <span className="patch-preview-label">{label}</span>}
      <ParamBar
        label="Cutoff"
        value={normCutoff(patch.filter.cutoff_hz)}
        displayValue={`${Math.round(patch.filter.cutoff_hz)} Hz`}
      />
      <ParamBar
        label="Res"
        value={patch.filter.resonance}
        displayValue={patch.filter.resonance.toFixed(2)}
      />
      <ParamBar
        label="Attack"
        value={Math.min(patch.amp_env.attack_s / 4, 1)}
        displayValue={`${patch.amp_env.attack_s.toFixed(2)}s`}
      />
      <ParamBar
        label="Sustain"
        value={patch.amp_env.sustain}
        displayValue={patch.amp_env.sustain.toFixed(2)}
      />
      <ParamBar
        label="LFO"
        value={lfoDepth}
        displayValue={lfoDepth.toFixed(2)}
      />
      <ParamBar
        label="Reverb"
        value={patch.reverb.mix}
        displayValue={patch.reverb.mix.toFixed(2)}
      />
    </div>
  );
}
