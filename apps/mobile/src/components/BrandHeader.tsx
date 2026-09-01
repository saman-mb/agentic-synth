import { StyleSheet, Text, View } from 'react-native';
import { LibraryPeek } from './LibraryPeek';
import { colors, space, typeScale } from '../theme/tokens';

export function BrandHeader({
  libraryCount = 0,
  onLibraryPress,
}: {
  libraryCount?: number;
  onLibraryPress?: () => void;
}) {
  return (
    <View style={styles.root} accessibilityRole="header">
      <Text style={styles.wordmark}>Tambra</Text>
      <LibraryPeek count={libraryCount} onPress={onLibraryPress} />
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    paddingVertical: space['2'],
    alignItems: 'center',
    position: 'relative',
    width: '100%',
  },
  wordmark: {
    color: colors.text.primary,
    fontSize: typeScale.display.size,
    lineHeight: typeScale.display.lineHeight,
    fontWeight: typeScale.display.weight as '600',
    letterSpacing: -0.5,
  },
});
