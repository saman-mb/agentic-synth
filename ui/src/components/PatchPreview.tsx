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
  return (
    <div className="patch-preview" aria-label={label ?? 'Patch preview'}>
      {label && <span className="patch-preview-label">{label}</span>}
      <ParamBar
        label="Cutoff"
        value={normCutoff(patch.cutoffHz)}
        displayValue={`${Math.round(patch.cutoffHz)} Hz`}
      />
      <ParamBar
        label="Res"
        value={patch.resonance}
        displayValue={patch.resonance.toFixed(2)}
      />
      <ParamBar
        label="Attack"
        value={Math.min(patch.attackS / 4, 1)}
        displayValue={`${patch.attackS.toFixed(2)}s`}
      />
      <ParamBar
        label="Sustain"
        value={patch.sustainLevel}
        displayValue={patch.sustainLevel.toFixed(2)}
      />
      <ParamBar
        label="LFO"
        value={patch.lfoDepth}
        displayValue={patch.lfoDepth.toFixed(2)}
      />
      <ParamBar
        label="Reverb"
        value={patch.reverbMix}
        displayValue={patch.reverbMix.toFixed(2)}
      />
    </div>
  );
}
