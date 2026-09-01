import { Pressable, StyleSheet, Text, View } from 'react-native';
import { colors, radius, space, typeScale } from '../theme/tokens';

export interface ShapeSheetProps {
  onVariations: () => void;
  onKeep: () => void;
  onRegenerate: () => void;
  onNewIdea: () => void;
}

export function ShapeSheet({
  onVariations,
  onKeep,
  onRegenerate,
  onNewIdea,
}: ShapeSheetProps) {
  return (
    <View style={styles.root} accessibilityLabel="Shape sheet">
      <View style={styles.grabber} />
      <Text style={styles.title}>Shape</Text>
      <Text style={styles.body}>Thumb the macros above. Swipe the visualizer for variations.</Text>
      <View style={styles.row}>
        <ActionButton label="Variations" onPress={onVariations} primary />
        <ActionButton label="Keep" onPress={onKeep} />
      </View>
      <View style={styles.row}>
        <ActionButton label="Regenerate" onPress={onRegenerate} />
        <ActionButton label="New idea" onPress={onNewIdea} />
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
      accessibilityLabel={label}
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
    minHeight: 160,
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
    marginBottom: space['4'],
  },
  row: {
    flexDirection: 'row',
    gap: space['3'],
    marginBottom: space['3'],
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
