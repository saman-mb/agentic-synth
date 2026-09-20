import React from 'react';
import { View, Text, StyleSheet, Pressable } from 'react-native';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import Animated, { useAnimatedStyle, useSharedValue, runOnJS } from 'react-native-reanimated';
import { colors, radius, space, typeScale } from '../theme/tokens';
import { MACRO_LABELS } from '../macros/macroProjection';

export interface PatchCardProps {
  macros: number[];  // [brightness, movement, space, body] each 0–1
  isActive: boolean; // is this the currently playing patch?
  onMacroChange: (index: number, value: number) => void;
  onActivate: () => void;  // tap the card to make it the active patch
  onSave: () => void;      // swipe left or tap save icon
}

const Slider = ({ label, value, onChange }: { label: string, value: number, onChange: (val: number) => void }) => {
  const width = useSharedValue(0);
  const progress = useSharedValue(value);

  React.useEffect(() => {
    progress.value = value;
  }, [value, progress]);

  const pan = Gesture.Pan()
    .onUpdate((e) => {
      if (width.value > 0) {
        let newVal = e.x / width.value;
        if (newVal < 0) newVal = 0;
        if (newVal > 1) newVal = 1;
        progress.value = newVal;
        runOnJS(onChange)(newVal);
      }
    });

  const trackStyle = useAnimatedStyle(() => {
    return {
      width: `${progress.value * 100}%`,
    };
  });
  
  const thumbStyle = useAnimatedStyle(() => {
    return {
      left: `${progress.value * 100}%`,
    };
  });

  return (
    <View style={styles.sliderContainer}>
      <Text style={styles.sliderLabel}>{label}</Text>
      <GestureDetector gesture={pan}>
        <View 
          style={styles.sliderTrackWrapper} 
          onLayout={(e) => { width.value = e.nativeEvent.layout.width; }}
        >
          <View style={styles.sliderTrack}>
            <Animated.View style={[styles.sliderFill, trackStyle]} />
          </View>
          <Animated.View style={[styles.sliderThumb, thumbStyle]} />
        </View>
      </GestureDetector>
    </View>
  );
};

export const PatchCard: React.FC<PatchCardProps> = ({
  macros,
  isActive,
  onMacroChange,
  onActivate,
  onSave,
}) => {
  return (
    <Pressable onPress={onActivate} style={[styles.card, isActive && styles.cardActive]}>
      <View style={styles.content}>
        {macros.map((val, index) => (
          <Slider 
            key={index} 
            label={(MACRO_LABELS && MACRO_LABELS[index]) ? MACRO_LABELS[index].toUpperCase() : `MACRO ${index + 1}`} 
            value={val} 
            onChange={(newVal) => onMacroChange(index, newVal)} 
          />
        ))}
        <Pressable onPress={onSave} style={styles.saveButton} accessibilityLabel="Save patch">
          <Text style={styles.saveIcon}>♡</Text>
        </Pressable>
      </View>
    </Pressable>
  );
};

const styles = StyleSheet.create({
  card: {
    backgroundColor: colors.bg.raised,
    borderRadius: radius.lg,
    padding: space.md,
    marginTop: space.sm,
    marginBottom: space.sm,
    borderLeftWidth: 3,
    borderLeftColor: 'transparent',
  },
  cardActive: {
    borderLeftColor: colors.accent.primary,
  },
  content: {
    position: 'relative',
  },
  sliderContainer: {
    marginBottom: space.sm,
  },
  sliderLabel: {
    ...typeScale.macroLabelStyle,
    color: colors.text.secondary,
    marginBottom: space.xs,
  },
  sliderTrackWrapper: {
    height: 44, // Minimum touch target 44dp
    justifyContent: 'center',
    position: 'relative',
  },
  sliderTrack: {
    height: 4,
    backgroundColor: colors.control.sliderTrack,
    borderRadius: radius.full,
    overflow: 'hidden',
  },
  sliderFill: {
    height: '100%',
    backgroundColor: colors.control.sliderFill,
  },
  sliderThumb: {
    position: 'absolute',
    top: '50%',
    marginTop: -8, // half of height
    marginLeft: -8, // half of width
    width: 16,
    height: 16,
    borderRadius: 8,
    backgroundColor: colors.accent.primary,
  },
  saveButton: {
    position: 'absolute',
    bottom: 0,
    right: 0,
    width: 44,
    height: 44,
    alignItems: 'flex-end',
    justifyContent: 'flex-end',
  },
  saveIcon: {
    fontSize: 20,
    color: colors.text.primary,
  }
});
