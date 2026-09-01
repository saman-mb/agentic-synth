import { StyleSheet, Text, View } from 'react-native';
import { colors, typeScale } from '../theme/tokens';

export function BrandHeader() {
  return (
    <View style={styles.root} accessibilityRole="header">
      <Text style={styles.wordmark}>Tambra</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    paddingVertical: 8,
    alignItems: 'center',
  },
  wordmark: {
    color: colors.text.primary,
    fontSize: typeScale.display.size,
    lineHeight: typeScale.display.lineHeight,
    fontWeight: typeScale.display.weight as '600',
    letterSpacing: -0.5,
  },
});
