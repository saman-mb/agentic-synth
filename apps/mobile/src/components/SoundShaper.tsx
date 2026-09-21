import React, { useRef, useState } from 'react';
import {
  PanResponder,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { colors, radius, space, typeScale } from '../theme/tokens';
import { MACRO_LABELS } from '../macros/macroProjection';

export interface SoundShaperProps {
  macros: readonly number[];
  onMacroChange: (index: number, value: number) => void;
  octaveOffset: number;
  onOctaveChange: (offset: number) => void;
  isVisible: boolean;
  onToggleVisible: () => void;
}

const MACRO_ICONS = ['🔆', '〰️', '🌌', '⚡'] as const;
const MACRO_SUBTITLES = ['Filter / Shimmer', 'Motion / Pulse', 'Reverb / Delay', 'Punch / Drive'] as const;

interface MacroRibbonProps {
  index: number;
  name: string;
  icon: string;
  subtitle: string;
  value: number;
  onChange: (value: number) => void;
}

const MacroRibbon: React.FC<MacroRibbonProps> = ({
  name,
  icon,
  subtitle,
  value,
  onChange,
}) => {
  const [isDragging, setIsDragging] = useState(false);
  const layoutHeight = useRef(110);
  const startY = useRef(0);
  const startVal = useRef(value);

  const panResponder = useRef(
    PanResponder.create({
      onStartShouldSetPanResponder: () => true,
      onMoveShouldSetPanResponder: () => true,
      onPanResponderGrant: (evt) => {
        setIsDragging(true);
        startY.current = evt.nativeEvent.locationY;
        startVal.current = value;
      },
      onPanResponderMove: (_evt, gestureState) => {
        // Drag up = increase, drag down = decrease
        const delta = -gestureState.dy / layoutHeight.current;
        const nextVal = Math.min(1, Math.max(0, startVal.current + delta));
        onChange(nextVal);
      },
      onPanResponderRelease: () => {
        setIsDragging(false);
      },
      onPanResponderTerminate: () => {
        setIsDragging(false);
      },
    })
  ).current;

  const percent = Math.round(value * 100);

  return (
    <View
      style={[styles.ribbonContainer, isDragging && styles.ribbonActive]}
      {...panResponder.panHandlers}
      onLayout={(e) => {
        layoutHeight.current = e.nativeEvent.layout.height || 110;
      }}
    >
      {/* Background fill track */}
      <View
        style={[
          styles.ribbonFill,
          {
            height: `${percent}%`,
            backgroundColor: isDragging ? colors.accent.glow : colors.accent.primary,
          },
        ]}
      />

      {/* Ribbon Labels */}
      <View style={styles.ribbonContent} pointerEvents="none">
        <Text style={styles.ribbonIcon}>{icon}</Text>
        <Text style={styles.ribbonPercent}>{percent}%</Text>
        <Text style={styles.ribbonName}>{name}</Text>
        <Text style={styles.ribbonSub}>{subtitle}</Text>
      </View>
    </View>
  );
};

export const SoundShaper: React.FC<SoundShaperProps> = ({
  macros,
  onMacroChange,
  octaveOffset,
  onOctaveChange,
  isVisible,
  onToggleVisible,
}) => {
  if (!isVisible) {
    return (
      <View style={styles.collapsedWrapper}>
        <TouchableOpacity
          style={styles.tweakButton}
          onPress={onToggleVisible}
          accessibilityRole="button"
          accessibilityLabel="Open sound controls"
          activeOpacity={0.8}
        >
          <Text style={styles.tweakButtonIcon}>🎛</Text>
          <Text style={styles.tweakButtonText}>Sound Controls</Text>
          <View style={styles.octaveBadge}>
            <Text style={styles.octaveBadgeText}>
              {octaveOffset === 0 ? 'MID' : octaveOffset > 0 ? `+${octaveOffset} OCT` : `${octaveOffset} OCT`}
            </Text>
          </View>
        </TouchableOpacity>
      </View>
    );
  }

  return (
    <View style={styles.shaperContainer}>
      {/* Top bar with Title, Octave switch, and Collapse button */}
      <View style={styles.shaperHeader}>
        <View style={styles.shaperTitleGroup}>
          <Text style={styles.shaperTitle}>🎛 Sound Shaper</Text>
          <Text style={styles.shaperSubtitle}>Drag ribbons to morph sound</Text>
        </View>

        {/* Octave Shifter */}
        <View style={styles.octaveControls}>
          <TouchableOpacity
            style={[styles.octaveBtn, octaveOffset <= -2 && styles.octaveBtnDisabled]}
            onPress={() => onOctaveChange(Math.max(-2, octaveOffset - 1))}
            disabled={octaveOffset <= -2}
          >
            <Text style={styles.octaveBtnText}>▼</Text>
          </TouchableOpacity>
          <Text style={styles.octaveValueText}>
            {octaveOffset === 0 ? 'C3' : octaveOffset > 0 ? `C${3 + octaveOffset}` : `C${3 + octaveOffset}`}
          </Text>
          <TouchableOpacity
            style={[styles.octaveBtn, octaveOffset >= 2 && styles.octaveBtnDisabled]}
            onPress={() => onOctaveChange(Math.min(2, octaveOffset + 1))}
            disabled={octaveOffset >= 2}
          >
            <Text style={styles.octaveBtnText}>▲</Text>
          </TouchableOpacity>
        </View>

        {/* Close Button */}
        <TouchableOpacity
          style={styles.closeBtn}
          onPress={onToggleVisible}
          accessibilityRole="button"
          accessibilityLabel="Close sound controls"
        >
          <Text style={styles.closeBtnText}>✕</Text>
        </TouchableOpacity>
      </View>

      {/* 4 Macro Vertical Touch Ribbons */}
      <View style={styles.ribbonsRow}>
        {MACRO_LABELS.map((name, i) => (
          <MacroRibbon
            key={name}
            index={i}
            name={name}
            icon={MACRO_ICONS[i]}
            subtitle={MACRO_SUBTITLES[i]}
            value={macros[i] ?? 0.5}
            onChange={(val) => onMacroChange(i, val)}
          />
        ))}
      </View>
    </View>
  );
};

const styles = StyleSheet.create({
  collapsedWrapper: {
    paddingHorizontal: space.chromePadX,
    paddingBottom: space['2'],
    alignItems: 'center',
  },
  tweakButton: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: colors.bg.raised,
    paddingVertical: 7,
    paddingHorizontal: 14,
    borderRadius: 20,
    borderWidth: 1,
    borderColor: colors.border.subtle,
    gap: 8,
  },
  tweakButtonIcon: {
    fontSize: 14,
  },
  tweakButtonText: {
    color: colors.text.secondary,
    fontSize: typeScale.caption.size,
    fontWeight: '600',
  },
  octaveBadge: {
    backgroundColor: colors.bg.inset,
    paddingHorizontal: 6,
    paddingVertical: 2,
    borderRadius: 8,
  },
  octaveBadgeText: {
    color: colors.accent.primary,
    fontSize: 10,
    fontWeight: '700',
  },
  shaperContainer: {
    backgroundColor: colors.bg.raised,
    marginHorizontal: space.chromePadX,
    marginBottom: space['2'],
    borderRadius: radius.lg,
    padding: space['3'],
    borderWidth: 1,
    borderColor: colors.border.subtle,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.35,
    shadowRadius: 8,
    elevation: 5,
  },
  shaperHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: space['2'],
  },
  shaperTitleGroup: {
    flex: 1,
  },
  shaperTitle: {
    color: colors.text.primary,
    fontSize: typeScale.caption.size,
    fontWeight: '700',
    letterSpacing: 0.3,
  },
  shaperSubtitle: {
    color: colors.text.tertiary,
    fontSize: 10,
  },
  octaveControls: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: colors.bg.inset,
    borderRadius: 14,
    paddingHorizontal: 4,
    paddingVertical: 2,
    gap: 4,
    marginRight: space['2'],
  },
  octaveBtn: {
    paddingHorizontal: 6,
    paddingVertical: 3,
  },
  octaveBtnDisabled: {
    opacity: 0.3,
  },
  octaveBtnText: {
    color: colors.accent.primary,
    fontSize: 10,
    fontWeight: '800',
  },
  octaveValueText: {
    color: colors.text.secondary,
    fontSize: 11,
    fontWeight: '700',
    minWidth: 20,
    textAlign: 'center',
  },
  closeBtn: {
    width: 24,
    height: 24,
    borderRadius: 12,
    backgroundColor: colors.bg.inset,
    alignItems: 'center',
    justifyContent: 'center',
  },
  closeBtnText: {
    color: colors.text.tertiary,
    fontSize: 12,
    fontWeight: '600',
  },
  ribbonsRow: {
    flexDirection: 'row',
    gap: space['2'],
    height: 110,
  },
  ribbonContainer: {
    flex: 1,
    backgroundColor: colors.bg.inset,
    borderRadius: radius.md,
    overflow: 'hidden',
    position: 'relative',
    justifyContent: 'flex-end',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.06)',
  },
  ribbonActive: {
    borderColor: colors.accent.primary,
  },
  ribbonFill: {
    position: 'absolute',
    bottom: 0,
    left: 0,
    right: 0,
    opacity: 0.35,
    borderRadius: radius.md,
  },
  ribbonContent: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    bottom: 0,
    padding: space['2'],
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  ribbonIcon: {
    fontSize: 16,
  },
  ribbonPercent: {
    color: colors.text.primary,
    fontSize: 13,
    fontWeight: '700',
  },
  ribbonName: {
    color: colors.text.secondary,
    fontSize: 10,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.5,
  },
  ribbonSub: {
    color: colors.text.tertiary,
    fontSize: 8,
    textAlign: 'center',
  },
});
