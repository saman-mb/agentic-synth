import { MacroKnobRow } from './MacroKnob';

export interface MacroKnobsProps {
  active?: boolean;
  positions: number[];
  onChange: (index: number, value: number) => void;
  onDragStart?: () => void;
  onDragEnd?: () => void;
}

/** Macro row — interactive in shape (#318). */
export function MacroKnobs({
  active = false,
  positions,
  onChange,
  onDragStart,
  onDragEnd,
}: MacroKnobsProps) {
  return (
    <MacroKnobRow
      positions={positions}
      enabled={active}
      onChange={onChange}
      onDragStart={onDragStart}
      onDragEnd={onDragEnd}
    />
  );
}
