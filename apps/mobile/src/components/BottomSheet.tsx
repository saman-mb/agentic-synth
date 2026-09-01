import { StyleSheet, Text, View } from 'react-native';
import type { MobileState } from '../state/mobileState';
import { colors, radius, space, typeScale } from '../theme/tokens';
import { KeepSheet } from './sheet/KeepSheet';
import { SaySheet, type SaySheetProps } from './sheet/SaySheet';
import { ShapeSheet } from './sheet/ShapeSheet';
import { VariationsSheet } from './sheet/VariationsSheet';
import type { VariationItem } from '../services/variationFlow';

const SHEET_COPY: Record<'idle' | 'hear' | 'error', { title: string; body: string }> = {
  idle: {
    title: 'Describe a sound',
    body: 'Tap Say to capture voice or text. Demo patch plays on first launch.',
  },
  hear: { title: 'Hear', body: 'Building your sound…' },
  error: { title: 'Something went wrong', body: 'Retry or dismiss.' },
};

export interface BottomSheetProps {
  state: MobileState;
  say?: Omit<SaySheetProps, never>;
  shape?: {
    onVariations: () => void;
    onKeep: () => void;
    onRegenerate: () => void;
    onNewIdea: () => void;
  };
  variations?: {
    items: VariationItem[];
    selectedIndex: number;
    loading: boolean;
    onSelect: (index: number) => void;
    onMore: () => void;
    onBack: () => void;
    onKeep: () => void;
  };
  keep?: {
    name: string;
    onNameChange: (name: string) => void;
    onConfirm: () => void;
    onCancel: () => void;
    saving?: boolean;
  };
}

export function BottomSheet({ state, say, shape, variations, keep }: BottomSheetProps) {
  if (state === 'say' && say) {
    return <SaySheet {...say} />;
  }
  if (state === 'shape' && shape) {
    return <ShapeSheet {...shape} />;
  }
  if (state === 'variations' && variations) {
    return <VariationsSheet {...variations} />;
  }
  if (state === 'keep' && keep) {
    return <KeepSheet {...keep} />;
  }

  const copy = SHEET_COPY[state as 'idle' | 'hear' | 'error'];
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
