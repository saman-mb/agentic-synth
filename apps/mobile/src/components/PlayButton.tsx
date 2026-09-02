import { Pressable, StyleSheet, Text } from 'react-native';
import { colors } from '../theme/tokens';

export function PlayButton({
  isPlaying,
  onPress,
}: {
  isPlaying: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      style={[styles.play, isPlaying && styles.playActive]}
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel={isPlaying ? 'Stop audition' : 'Play audition'}
    >
      <Text style={[styles.playLabel, isPlaying && styles.playLabelActive]}>
        {isPlaying ? '■' : '▶'}
      </Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  play: {
    width: 56,
    height: 56,
    borderRadius: 28,
    backgroundColor: colors.bg.raised,
    borderWidth: 2,
    borderColor: colors.accent.play,
    alignItems: 'center',
    justifyContent: 'center',
  },
  playActive: {
    backgroundColor: colors.accent.play,
  },
  playLabel: {
    color: colors.accent.play,
    fontSize: 20,
    fontWeight: '600',
  },
  playLabelActive: {
    color: colors.text.onAccent,
  },
});
