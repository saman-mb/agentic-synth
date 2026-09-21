import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  AccessibilityInfo,
  type GestureResponderEvent,
  type LayoutChangeEvent,
  StyleSheet,
  View,
} from 'react-native';
import { Canvas, Circle, Fill, Rect, RoundedRect } from '@shopify/react-native-skia';
import { colors } from '../theme/tokens';

const COLS = 8;
const ROWS = 4;
const BASE_OCTAVE = 3;
const PENTATONIC_MINOR = [0, 3, 5, 7, 10]; // semitone offsets
const TARGET_FPS = 30;
const FRAME_MS = 1000 / TARGET_FPS;

function gridToMidi(col: number, row: number, baseOctave = BASE_OCTAVE): number {
  const octave = baseOctave + row;
  const degree = col % PENTATONIC_MINOR.length;
  const octaveOffset = Math.floor(col / PENTATONIC_MINOR.length);
  return (octave + octaveOffset) * 12 + PENTATONIC_MINOR[degree];
}

interface TouchPoint {
  id: string;
  x: number;
  y: number;
  note: number;
  col: number;
  row: number;
  rippleRadius: number;
}

export interface PlaySurfaceProps {
  onNoteOn: (note: number, velocity: number) => void;
  onNoteOff: (note: number) => void;
  isPlaying: boolean;
  scopeSamples?: number[];
  octaveOffset?: number;
  onTouchStart?: () => void;
  onTouchEnd?: () => void;
}

export function PlaySurface({
  onNoteOn,
  onNoteOff,
  isPlaying,
  scopeSamples,
  octaveOffset = 0,
  onTouchStart,
  onTouchEnd,
}: PlaySurfaceProps) {
  const [layout, setLayout] = useState({ width: 0, height: 0 });
  const [phase, setPhase] = useState(0);
  const [reduceMotion, setReduceMotion] = useState(false);
  const [activeTouches, setActiveTouches] = useState<TouchPoint[]>([]);
  const touchesRef = useRef<Map<string, { note: number; x: number; y: number; col: number; row: number; rippleRadius: number }>>(
    new Map(),
  );

  useEffect(() => {
    void AccessibilityInfo.isReduceMotionEnabled().then(setReduceMotion);
    const sub = AccessibilityInfo.addEventListener('reduceMotionChanged', setReduceMotion);
    return () => sub.remove();
  }, []);

  // Idle animation & ripple expansion loop
  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    const tick = (now: number) => {
      if (now - last >= FRAME_MS) {
        if (!reduceMotion) {
          setPhase((p) => p + 0.08);
        }
        if (touchesRef.current.size > 0) {
          touchesRef.current.forEach((touch) => {
            touch.rippleRadius = (touch.rippleRadius + 3) % 80;
          });
          setActiveTouches(
            Array.from(touchesRef.current.entries()).map(([id, t]) => ({
              id,
              x: t.x,
              y: t.y,
              note: t.note,
              col: t.col,
              row: t.row,
              rippleRadius: t.rippleRadius,
            })),
          );
        }
        last = now;
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [reduceMotion]);

  const onLayout = useCallback((e: LayoutChangeEvent) => {
    setLayout({
      width: e.nativeEvent.layout.width,
      height: e.nativeEvent.layout.height,
    });
  }, []);

  const resolveTouch = useCallback(
    (x: number, y: number) => {
      const colWidth = layout.width / COLS;
      const rowHeight = layout.height / ROWS;
      const col = Math.min(COLS - 1, Math.max(0, Math.floor(x / Math.max(colWidth, 1))));
      const row = Math.min(ROWS - 1, Math.max(0, Math.floor(y / Math.max(rowHeight, 1))));
      // row 0 at bottom, row 3 at top
      const pitchRow = ROWS - 1 - row;
      const note = gridToMidi(col, pitchRow, BASE_OCTAVE + octaveOffset);
      const velocity = Math.min(127, Math.max(40, Math.round(40 + (1 - y / Math.max(layout.height, 1)) * 87)));
      return { col, row, note, velocity };
    },
    [layout, octaveOffset],
  );

  const handleTouchStart = useCallback(
    (e: GestureResponderEvent) => {
      if (layout.width === 0 || layout.height === 0) return;
      const wasEmpty = touchesRef.current.size === 0;

      for (let i = 0; i < e.nativeEvent.changedTouches.length; i++) {
        const touch = e.nativeEvent.changedTouches[i];
        const { col, row, note, velocity } = resolveTouch(touch.locationX, touch.locationY);
        touchesRef.current.set(String(touch.identifier), {
          note,
          x: touch.locationX,
          y: touch.locationY,
          col,
          row,
          rippleRadius: 8,
        });
        onNoteOn(note, velocity);
      }

      setActiveTouches(
        Array.from(touchesRef.current.entries()).map(([id, t]) => ({
          id,
          x: t.x,
          y: t.y,
          note: t.note,
          col: t.col,
          row: t.row,
          rippleRadius: t.rippleRadius,
        })),
      );

      if (wasEmpty && touchesRef.current.size > 0) {
        onTouchStart?.();
      }
    },
    [layout, onNoteOn, onTouchStart, resolveTouch],
  );

  const handleTouchMove = useCallback(
    (e: GestureResponderEvent) => {
      if (layout.width === 0 || layout.height === 0) return;

      for (let i = 0; i < e.nativeEvent.changedTouches.length; i++) {
        const touch = e.nativeEvent.changedTouches[i];
        const id = String(touch.identifier);
        const existing = touchesRef.current.get(id);
        if (!existing) continue;

        const { col, row, note, velocity } = resolveTouch(touch.locationX, touch.locationY);
        if (note !== existing.note) {
          onNoteOff(existing.note);
          onNoteOn(note, velocity);
        }
        existing.note = note;
        existing.x = touch.locationX;
        existing.y = touch.locationY;
        existing.col = col;
        existing.row = row;
      }

      setActiveTouches(
        Array.from(touchesRef.current.entries()).map(([id, t]) => ({
          id,
          x: t.x,
          y: t.y,
          note: t.note,
          col: t.col,
          row: t.row,
          rippleRadius: t.rippleRadius,
        })),
      );
    },
    [layout, onNoteOff, onNoteOn, resolveTouch],
  );

  const handleTouchEnd = useCallback(
    (e: GestureResponderEvent) => {
      for (let i = 0; i < e.nativeEvent.changedTouches.length; i++) {
        const touch = e.nativeEvent.changedTouches[i];
        const id = String(touch.identifier);
        const existing = touchesRef.current.get(id);
        if (existing) {
          onNoteOff(existing.note);
          touchesRef.current.delete(id);
        }
      }

      setActiveTouches(
        Array.from(touchesRef.current.entries()).map(([id, t]) => ({
          id,
          x: t.x,
          y: t.y,
          note: t.note,
          col: t.col,
          row: t.row,
          rippleRadius: t.rippleRadius,
        })),
      );

      if (touchesRef.current.size === 0) {
        onTouchEnd?.();
      }
    },
    [onNoteOff, onTouchEnd],
  );

  const colWidth = layout.width > 0 ? layout.width / COLS : 0;
  const rowHeight = layout.height > 0 ? layout.height / ROWS : 0;

  // Idle background waves
  const idleBars = useMemo(() => {
    if (layout.width === 0 || layout.height === 0) return [];
    const count = 32;
    const barWidth = layout.width / count;
    const midY = layout.height * 0.4;
    return Array.from({ length: count }, (_, i) => {
      const sample = scopeSamples?.[i % Math.max(scopeSamples?.length ?? 1, 1)] ?? 0;
      const energy = isPlaying || activeTouches.length > 0 ? Math.abs(sample) + 0.25 : 0.08;
      const wave = reduceMotion ? 0.7 : Math.sin(phase + i * 0.28) * 0.2 + 0.8;
      const h = Math.min(1, energy * wave) * 80;
      return {
        x: i * barWidth,
        y: midY - h / 2,
        w: barWidth * 0.55,
        h: Math.max(h, 4),
      };
    });
  }, [activeTouches.length, isPlaying, layout, phase, reduceMotion, scopeSamples]);

  return (
    <View
      style={styles.root}
      onLayout={onLayout}
      onStartShouldSetResponder={() => true}
      onMoveShouldSetResponder={() => true}
      onResponderGrant={handleTouchStart}
      onResponderMove={handleTouchMove}
      onResponderRelease={handleTouchEnd}
      onResponderTerminate={handleTouchEnd}
      accessibilityRole="adjustable"
      accessibilityLabel="Play surface — tap to play notes"
      accessibilityHint="Touch anywhere on the grid to play scale-locked notes"
    >
      <Canvas style={StyleSheet.absoluteFill}>
        <Fill color={colors.bg.inset} />

        {/* Subtle idle audio-reactive visualizer bars */}
        {idleBars.map((bar, i) => (
          <RoundedRect
            key={`bar-${i}`}
            x={bar.x}
            y={bar.y}
            width={bar.w}
            height={bar.h}
            r={2}
            color={colors.accent.viz}
            opacity={0.35}
          />
        ))}

        {/* Grid lines */}
        {layout.width > 0 &&
          layout.height > 0 &&
          Array.from({ length: COLS - 1 }, (_, i) => (
            <Rect
              key={`v-grid-${i}`}
              x={(i + 1) * colWidth}
              y={0}
              width={1}
              height={layout.height}
              color={colors.accent.viz}
              opacity={0.08}
            />
          ))}

        {layout.width > 0 &&
          layout.height > 0 &&
          Array.from({ length: ROWS - 1 }, (_, i) => (
            <Rect
              key={`h-grid-${i}`}
              x={0}
              y={(i + 1) * rowHeight}
              width={layout.width}
              height={1}
              color={colors.accent.viz}
              opacity={0.08}
            />
          ))}

        {/* Active touch cell highlights */}
        {colWidth > 0 &&
          rowHeight > 0 &&
          activeTouches.map((touch) => (
            <RoundedRect
              key={`cell-${touch.id}`}
              x={touch.col * colWidth + 2}
              y={touch.row * rowHeight + 2}
              width={colWidth - 4}
              height={rowHeight - 4}
              r={6}
              color={colors.accent.viz}
              opacity={0.25}
            />
          ))}

        {/* Expanding touch ripple / bloom */}
        {activeTouches.map((touch) => (
          <Circle
            key={`ripple-${touch.id}`}
            cx={touch.x}
            cy={touch.y}
            r={touch.rippleRadius}
            color={colors.accent.play}
            opacity={Math.max(0, 0.5 - touch.rippleRadius / 160)}
          />
        ))}
      </Canvas>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    width: '100%',
    height: '100%',
    overflow: 'hidden',
  },
});
