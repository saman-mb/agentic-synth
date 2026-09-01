import { StyleSheet, Text, View } from 'react-native';
import { colors, space, typeScale } from '../theme/tokens';

export function StatusLine({ message, backend }: { message: string; backend?: string }) {
  if (!message && !backend) return null;
  const line = [message, backend ? `Engine: ${backend}` : ''].filter(Boolean).join(' · ');
  return (
    <View style={styles.root}>
      <Text style={styles.text} numberOfLines={1}>
        {line}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    paddingHorizontal: space.chromePadX,
    paddingVertical: space['2'],
  },
  text: {
    color: colors.text.tertiary,
    fontSize: typeScale.caption.size,
    lineHeight: typeScale.caption.lineHeight,
  },
});
