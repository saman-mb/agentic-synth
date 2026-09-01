import { StyleSheet, Text, View } from 'react-native';
import type { MobileState } from '../state/mobileState';
import { colors, radius, space, typeScale } from '../theme/tokens';
import { SaySheet, type SaySheetProps } from './sheet/SaySheet';

const SHEET_COPY: Record<Exclude<MobileState, 'say'>, { title: string; body: string }> = {
  idle: {
    title: 'Describe a sound',
    body: 'Tap Say to capture voice or text. Demo patch plays on first launch.',
  },
  hear: { title: 'Hear', body: 'Building your sound…' },
  shape: { title: 'Shape', body: 'Thumb the macros — interaction lands in #318.' },
  variations: { title: 'Variations', body: 'Browse variants — stub (#319).' },
  keep: { title: 'Keep', body: 'Name and save — stub (#320).' },
  error: { title: 'Something went wrong', body: 'Retry or dismiss.' },
};

export interface BottomSheetProps {
  state: MobileState;
  say?: Omit<SaySheetProps, never>;
}

export function BottomSheet({ state, say }: BottomSheetProps) {
  if (state === 'say' && say) {
    return <SaySheet {...say} />;
  }

  const copy = SHEET_COPY[state as Exclude<MobileState, 'say'>];
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
