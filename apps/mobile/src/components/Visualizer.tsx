import { useEffect, useMemo, useState } from 'react';
import { LayoutChangeEvent, StyleSheet, View } from 'react-native';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import { runOnJS } from 'react-native-reanimated';
import { Canvas, Fill, RoundedRect } from '@shopify/react-native-skia';
import { colors, space } from '../theme/tokens';

const BAR_COUNT = 48;
const TARGET_FPS = 30;
const FRAME_MS = 1000 / TARGET_FPS;
const SWIPE_THRESHOLD = 48;

export function Visualizer({
  isPlaying,
  scopeSamples,
  height = 160,
  swipeEnabled = false,
  onSwipe,
}: {
  isPlaying: boolean;
  scopeSamples?: number[];
  height?: number;
  swipeEnabled?: boolean;
  onSwipe?: (direction: 1 | -1) => void;
}) {
  const [phase, setPhase] = useState(0);
  const [layoutWidth, setLayoutWidth] = useState(0);

  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    const tick = (now: number) => {
      if (now - last >= FRAME_MS) {
        setPhase((p) => p + 0.08);
        last = now;
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  const pan = Gesture.Pan()
    .enabled(swipeEnabled && !!onSwipe)
    .activeOffsetX([-16, 16])
    .failOffsetY([-24, 24])
    .onEnd((e) => {
      if (!onSwipe) return;
      if (e.translationX <= -SWIPE_THRESHOLD) runOnJS(onSwipe)(1);
      else if (e.translationX >= SWIPE_THRESHOLD) runOnJS(onSwipe)(-1);
    });

  const bars = useMemo(() => {
    const usable = Math.max(layoutWidth - space.chromePadX * 2, 1);
    const barWidth = usable / BAR_COUNT;
    return Array.from({ length: BAR_COUNT }, (_, i) => {
      const sample = scopeSamples?.[i % Math.max(scopeSamples?.length ?? 1, 1)] ?? 0;
      const energy = isPlaying ? Math.abs(sample) + 0.18 : 0.07;
      const wave = Math.sin(phase + i * 0.35) * 0.22 + 0.78;
      const h = Math.min(1, energy * wave) * (height - 20);
      const x = space.chromePadX + i * barWidth;
      return { x, y: height - h - 8, w: barWidth * 0.62, h: Math.max(h, 3) };
    });
  }, [height, isPlaying, layoutWidth, phase, scopeSamples]);

  const onLayout = (e: LayoutChangeEvent) => {
    setLayoutWidth(e.nativeEvent.layout.width);
  };

  return (
    <GestureDetector gesture={pan}>
      <View
        style={[styles.root, { height }]}
        onLayout={onLayout}
        accessibilityLabel="Audio visualizer"
        accessibilityHint={swipeEnabled ? 'Swipe left or right for variations' : undefined}
      >
        <Canvas style={StyleSheet.absoluteFill}>
          <Fill color={colors.bg.inset} />
          {bars.map((bar, i) => (
            <RoundedRect
              key={i}
              x={bar.x}
              y={bar.y}
              width={bar.w}
              height={bar.h}
              r={2}
              color={colors.accent.viz}
              opacity={0.88}
            />
          ))}
        </Canvas>
      </View>
    </GestureDetector>
  );
}

const styles = StyleSheet.create({
  root: {
    width: '100%',
    overflow: 'hidden',
    borderRadius: 10,
    marginVertical: space['3'],
  },
});
