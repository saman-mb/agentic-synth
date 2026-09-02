import { Pressable, StyleSheet, Text, View } from 'react-native';
import { colors, radius, space, typeScale } from '../../theme/tokens';

export interface HearSheetProps {
  message: string;
  onCancel?: () => void;
}

export function HearSheet({ message, onCancel }: HearSheetProps) {
  return (
    <View style={styles.root} accessibilityLabel="Hear sheet">
      <View style={styles.grabber} />
      <Text style={styles.title}>Hear</Text>
      <Text style={styles.body}>{message || 'Building your sound…'}</Text>
      {onCancel ? (
        <Pressable
          style={({ pressed, focused }) => [
            styles.btn,
            (pressed || focused) && styles.btnFocus,
          ]}
          onPress={onCancel}
          accessibilityRole="button"
          accessibilityLabel="Cancel generate"
        >
          <Text style={styles.btnText}>Cancel</Text>
        </Pressable>
      ) : null}
    </View>
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
    minHeight: 140,
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
  btn: {
    minHeight: space['9'],
    paddingVertical: space['3'],
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border.strong,
    alignItems: 'center',
    justifyContent: 'center',
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
});
