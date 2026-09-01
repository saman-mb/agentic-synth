import { Pressable, StyleSheet, Text } from 'react-native';
import { colors, radius, space, typeScale } from '../theme/tokens';

/** Weak library entry — peek only per docs/mobile/ia.md `el.library`. */
export function LibraryPeek({
  count,
  onPress,
}: {
  count: number;
  onPress?: () => void;
}) {
  return (
    <Pressable
      style={({ pressed, focused }) => [
        styles.root,
        (pressed || focused) && styles.focus,
      ]}
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel={`Library, ${count} saved`}
    >
      <Text style={styles.label}>Library</Text>
      {count > 0 ? <Text style={styles.count}>{count}</Text> : null}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: {
    position: 'absolute',
    right: 0,
    top: 0,
    minHeight: space['9'],
    minWidth: space['9'],
    flexDirection: 'row',
    alignItems: 'center',
    gap: space['1'],
    paddingHorizontal: space['3'],
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border.subtle,
  },
  focus: {
    borderColor: colors.border.focus,
    borderWidth: 2,
  },
  label: {
    color: colors.text.secondary,
    fontSize: typeScale.caption.size,
    fontWeight: '600',
  },
  count: {
    color: colors.accent.primary,
    fontSize: typeScale.caption.size,
    fontWeight: '600',
  },
});
