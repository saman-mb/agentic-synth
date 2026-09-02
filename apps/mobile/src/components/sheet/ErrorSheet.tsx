import { Pressable, StyleSheet, Text, View } from 'react-native';
import { colors, radius, space, typeScale } from '../../theme/tokens';

export interface ErrorSheetProps {
  message: string;
  onRetry?: () => void;
  onDismiss: () => void;
}

export function ErrorSheet({ message, onRetry, onDismiss }: ErrorSheetProps) {
  return (
    <View style={styles.root} accessibilityLabel="Error sheet">
      <View style={styles.grabber} />
      <Text style={styles.title}>Something went wrong</Text>
      <Text style={styles.body}>{message || 'Try again or go back.'}</Text>
      <View style={styles.row}>
        {onRetry ? (
          <ActionButton label="Retry" onPress={onRetry} primary />
        ) : null}
        <ActionButton label="Dismiss" onPress={onDismiss} primary={!onRetry} />
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
      style={({ pressed, focused }) => [
        styles.btn,
        primary && styles.btnPrimary,
        (pressed || focused) && styles.btnFocus,
      ]}
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
  },
  btn: {
    flex: 1,
    minHeight: space['9'],
    paddingVertical: space['3'],
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border.strong,
    alignItems: 'center',
    justifyContent: 'center',
  },
  btnPrimary: {
    backgroundColor: colors.accent.primary,
    borderColor: colors.accent.primary,
  },
  btnFocus: {
    borderColor: colors.border.focus,
    borderWidth: 2,
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
