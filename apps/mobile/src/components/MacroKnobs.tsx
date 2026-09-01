import { StyleSheet, Text, View } from 'react-native';
import { colors, radius, space, typeScale } from '../theme/tokens';

const MACROS = [
  { id: 'macro.0', label: 'Brightness' },
  { id: 'macro.1', label: 'Movement' },
  { id: 'macro.2', label: 'Space' },
  { id: 'macro.3', label: 'Body' },
] as const;

/** Stub macro row — interaction deferred to #317+. */
export function MacroKnobs() {
  return (
    <View style={styles.plate} accessibilityLabel="Macro controls">
      {MACROS.map((macro) => (
        <View key={macro.id} style={styles.knobWrap}>
          <View style={styles.knob} accessibilityState={{ disabled: true }}>
            <View style={styles.arc} />
          </View>
          <Text style={styles.label}>{macro.label}</Text>
        </View>
      ))}
    </View>
  );
}

const KNOB = 44;
const HIT = 56;

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
  knobWrap: {
    alignItems: 'center',
    width: HIT,
  },
  knob: {
    width: HIT,
    height: HIT,
    alignItems: 'center',
    justifyContent: 'center',
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
