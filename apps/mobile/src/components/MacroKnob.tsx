import { StyleSheet, Text, View } from 'react-native';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import Animated, { runOnJS, useSharedValue } from 'react-native-reanimated';

import { MACRO_LABELS } from '../macros/macroProjection';
import { colors, radius, space, typeScale } from '../theme/tokens';

const KNOB = 44;
const HIT = 56;

export interface MacroKnobProps {
  index: number;
  label: string;
  value: number;
  enabled: boolean;
  onChange: (index: number, value: number) => void;
  onDragStart?: () => void;
  onDragEnd?: () => void;
}

export function MacroKnob({
  index,
  label,
  value,
  enabled,
  onChange,
  onDragStart,
  onDragEnd,
}: MacroKnobProps) {
  const startY = useSharedValue(0);
  const startVal = useSharedValue(value);

  const pan = Gesture.Pan()
    .enabled(enabled)
    .onBegin(() => {
      startY.value = 0;
      startVal.value = value;
      if (onDragStart) runOnJS(onDragStart)();
    })
    .onUpdate((e) => {
      const delta = -(e.translationY - startY.value) / 120;
      const next = Math.min(1, Math.max(0, startVal.value + delta));
      runOnJS(onChange)(index, next);
    })
    .onFinalize(() => {
      if (onDragEnd) runOnJS(onDragEnd)();
    });

  const arcRotation = `${value * 270 - 135}deg`;

  return (
    <View style={styles.wrap}>
      <GestureDetector gesture={pan}>
        <Animated.View
          style={[styles.hit, !enabled && styles.disabled]}
          accessibilityRole="adjustable"
          accessibilityLabel={`${label} macro`}
          accessibilityValue={{ min: 0, max: 1, now: value }}
        >
          <View style={[styles.arc, { transform: [{ rotate: arcRotation }] }]} />
        </Animated.View>
      </GestureDetector>
      <Text style={styles.label}>{label}</Text>
    </View>
  );
}

export function MacroKnobRow({
  positions,
  enabled,
  onChange,
  onDragStart,
  onDragEnd,
}: {
  positions: number[];
  enabled: boolean;
  onChange: (index: number, value: number) => void;
  onDragStart?: () => void;
  onDragEnd?: () => void;
}) {
  return (
    <View style={styles.plate} accessibilityLabel="Macro controls">
      {MACRO_LABELS.map((label, index) => (
        <MacroKnob
          key={label}
          index={index}
          label={label}
          value={positions[index] ?? 0.5}
          enabled={enabled}
          onChange={onChange}
          onDragStart={onDragStart}
          onDragEnd={onDragEnd}
        />
      ))}
    </View>
  );
}

const styles = StyleSheet.create({
  plate: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    backgroundColor: colors.bg.knobPlate,
    borderRadius: radius.lg,
    paddingHorizontal: space.chromePadX,
    paddingVertical: space['4'],
    gap: space.macrosGap,
  },
  wrap: {
    alignItems: 'center',
    width: HIT,
  },
  hit: {
    width: HIT,
    height: HIT,
    alignItems: 'center',
    justifyContent: 'center',
  },
  disabled: {
    opacity: 0.55,
  },
  arc: {
    width: KNOB,
    height: KNOB,
    borderRadius: radius.knob,
    borderWidth: 3,
    borderColor: colors.control.knobRing,
    borderTopColor: colors.control.knobArc,
    borderRightColor: colors.control.knobArc,
  },
  label: {
    marginTop: space['2'],
    color: colors.text.secondary,
    fontSize: typeScale.macroLabel.size,
    lineHeight: typeScale.macroLabel.lineHeight,
    fontWeight: typeScale.macroLabel.weight as '500',
    textTransform: 'uppercase',
  },
});
