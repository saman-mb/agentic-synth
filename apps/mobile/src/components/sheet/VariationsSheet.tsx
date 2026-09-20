import { Pressable, StyleSheet, Text, View } from 'react-native';
import type { VariationItem } from '../../services/variationFlow';
import { colors, radius, space, typeScale } from '../../theme/tokens';

export interface VariationsSheetProps {
  variations: VariationItem[];
  selectedIndex: number;
  loading: boolean;
  onSelect: (index: number) => void;
  onMore: () => void;
  onBack: () => void;
  onKeep: () => void;
}

export function VariationsSheet({
  variations,
  selectedIndex,
  loading,
  onSelect,
  onMore,
  onBack,
  onKeep,
}: VariationsSheetProps) {
  return (
    <View style={styles.root} accessibilityLabel="Variations sheet">
      <View style={styles.grabber} />
      <Text style={styles.title}>Variations</Text>
      <Text style={styles.body}>Swipe the visualizer or pick a variant below.</Text>
      <View style={styles.list}>
        {variations.map((v) => (
          <Pressable
            key={v.index}
            style={[styles.chip, v.index === selectedIndex && styles.chipActive]}
            onPress={() => onSelect(v.index)}
            accessibilityRole="button"
            accessibilityState={{ selected: v.index === selectedIndex }}
          >
            <Text style={styles.chipText}>
              #{v.index + 1} {v.source === 'local' ? '· local' : ''}
            </Text>
          </Pressable>
        ))}
        {loading ? <Text style={styles.loading}>Loading…</Text> : null}
      </View>
      <View style={styles.row}>
        <ActionButton label="Back" onPress={onBack} />
        <ActionButton label="More" onPress={onMore} />
        <ActionButton label="Keep" onPress={onKeep} primary />
      </View>
    </View>
  );
}

function ActionButton({
  label,
  onPress,
  primary = false,
}: {
  label: string;
  onPress: () => void;
  primary?: boolean;
}) {
  return (
    <Pressable
      style={[styles.btn, primary && styles.btnPrimary]}
      onPress={onPress}
      accessibilityRole="button"
    >
      <Text style={[styles.btnText, primary && styles.btnTextPrimary]}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: {
    backgroundColor: colors.bg.sheet,
    borderTopLeftRadius: radius.sheet,
    borderTopRightRadius: radius.sheet,
    paddingHorizontal: space.chromePadX,
    paddingTop: space['3'],
    paddingBottom: space.safeBottomMin,
    minHeight: 200,
    borderTopWidth: 1,
    borderColor: colors.border.subtle,
  },
  grabber: {
    alignSelf: 'center',
    width: 40,
    height: 4,
    borderRadius: 2,
    backgroundColor: colors.border.strong,
    marginBottom: space['3'],
  },
  title: {
    color: colors.text.primary,
    fontSize: typeScale.title.size,
    fontWeight: typeScale.title.weight as '600',
    marginBottom: space['2'],
  },
  body: {
    color: colors.text.secondary,
    fontSize: typeScale.body.size,
    lineHeight: typeScale.body.lineHeight,
    marginBottom: space['3'],
  },
  list: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: space['2'],
    marginBottom: space['4'],
  },
  chip: {
    paddingHorizontal: space['3'],
    paddingVertical: space['2'],
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border.strong,
  },
  chipActive: {
    borderColor: colors.accent.primary,
    backgroundColor: colors.bg.inset,
  },
  chipText: {
    color: colors.text.primary,
    fontSize: typeScale.body.size,
  },
  loading: {
    color: colors.text.secondary,
    fontSize: typeScale.body.size,
  },
  row: {
    flexDirection: 'row',
    gap: space['2'],
  },
  btn: {
    flex: 1,
    paddingVertical: space['3'],
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border.strong,
    alignItems: 'center',
  },
  btnPrimary: {
    backgroundColor: colors.accent.primary,
    borderColor: colors.accent.primary,
  },
  btnText: {
    color: colors.text.primary,
    fontSize: typeScale.body.size,
    fontWeight: '600',
  },
  btnTextPrimary: {
    color: colors.text.onAccent,
  },
});
