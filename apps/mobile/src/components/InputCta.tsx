import { Pressable, StyleSheet, Text } from 'react-native';
import { colors, radius, space, typeScale } from '../theme/tokens';

/** Stub Say affordance — voice/text capture deferred to #317. */
export function InputCta({ onPress }: { onPress?: () => void }) {
  return (
    <Pressable
      style={styles.root}
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel="Describe a sound"
      accessibilityState={{ disabled: true }}
    >
      <Text style={styles.icon}>🎙</Text>
      <Text style={styles.label}>Say</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space['2'],
    paddingHorizontal: space['5'],
    paddingVertical: space['3'],
    borderRadius: radius.pill,
    backgroundColor: colors.bg.raised,
    borderWidth: 1,
    borderColor: colors.border.subtle,
    opacity: 0.6,
  },
  icon: {
    fontSize: 18,
  },
  label: {
    color: colors.text.primary,
    fontSize: typeScale.bodyStrong.size,
    fontWeight: typeScale.bodyStrong.weight as '600',
  },
});
