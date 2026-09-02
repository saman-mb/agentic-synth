import { Pressable, StyleSheet, Text, TextInput, View } from 'react-native';
import { colors, radius, space, typeScale } from '../../theme/tokens';

export interface KeepSheetProps {
  name: string;
  onNameChange: (name: string) => void;
  onConfirm: () => void;
  onCancel: () => void;
  saving?: boolean;
}

export function KeepSheet({
  name,
  onNameChange,
  onConfirm,
  onCancel,
  saving = false,
}: KeepSheetProps) {
  return (
    <View style={styles.root} accessibilityLabel="Keep sheet">
      <View style={styles.grabber} />
      <Text style={styles.title}>Keep</Text>
      <Text style={styles.body}>Name this sound — it saves locally on your device.</Text>
      <TextInput
        style={styles.input}
        value={name}
        onChangeText={onNameChange}
        placeholder="Patch name"
        placeholderTextColor={colors.text.tertiary}
        accessibilityLabel="Preset name"
      />
      <View style={styles.row}>
        <ActionButton label="Cancel" onPress={onCancel} disabled={saving} />
        <ActionButton label="Confirm Keep" onPress={onConfirm} primary disabled={saving} />
      </View>
    </View>
  );
}

function ActionButton({
  label,
  onPress,
  primary = false,
  disabled = false,
}: {
  label: string;
  onPress: () => void;
  primary?: boolean;
  disabled?: boolean;
}) {
  return (
    <Pressable
      style={[styles.btn, primary && styles.btnPrimary, disabled && styles.disabled]}
      onPress={onPress}
      disabled={disabled}
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
    marginBottom: space['4'],
  },
  input: {
    borderWidth: 1,
    borderColor: colors.border.strong,
    borderRadius: radius.md,
    paddingHorizontal: space['3'],
    paddingVertical: space['3'],
    color: colors.text.primary,
    fontSize: typeScale.body.size,
    marginBottom: space['4'],
  },
  row: {
    flexDirection: 'row',
    gap: space['3'],
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
  disabled: {
    opacity: 0.5,
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
