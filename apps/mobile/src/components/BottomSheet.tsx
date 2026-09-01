import { StyleSheet, Text, View } from 'react-native';
import type { MobileState } from '../state/mobileState';
import { colors, radius, space, typeScale } from '../theme/tokens';

const SHEET_COPY: Record<MobileState, { title: string; body: string }> = {
  idle: {
    title: 'Describe a sound',
    body: 'Tap Say to capture voice or text. Demo patch plays on first launch.',
  },
  say: { title: 'Say', body: 'Voice and text input — stub (#317).' },
  hear: { title: 'Hear', body: 'Building your sound… Demo patch audition active.' },
  shape: { title: 'Shape', body: 'Adjust macros and play — stub (#318).' },
  variations: { title: 'Variations', body: 'Browse variants — stub (#319).' },
  keep: { title: 'Keep', body: 'Name and save — stub (#320).' },
  error: { title: 'Something went wrong', body: 'Retry or dismiss.' },
};

export function BottomSheet({ state }: { state: MobileState }) {
  const copy = SHEET_COPY[state];
  return (
    <View style={styles.root} accessibilityLabel={`Sheet ${state}`}>
      <View style={styles.grabber} />
      <Text style={styles.title}>{copy.title}</Text>
      <Text style={styles.body}>{copy.body}</Text>
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
    minHeight: 120,
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
  },
});
