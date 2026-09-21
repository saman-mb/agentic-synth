import { useState } from 'react';
import {
  Animated,
  KeyboardAvoidingView,
  Platform,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { PlaySurface } from '../src/components/PlaySurface';
import { ChatThread } from '../src/components/ChatThread';
import { PromptBar } from '../src/components/PromptBar';
import { SoundShaper } from '../src/components/SoundShaper';
import { useMobileApp } from '../src/hooks/useMobileApp';
import { colors, space } from '../src/theme/tokens';

export default function HomeScreen() {
  const app = useMobileApp();
  const [isChatVisible, setIsChatVisible] = useState(false);
  const [isShaperVisible, setIsShaperVisible] = useState(false);

  return (
    <SafeAreaView style={styles.safe} edges={['top', 'left', 'right']}>
      <View style={styles.root}>
        {/* Minimal wordmark */}
        <View style={styles.header}>
          <Text style={styles.wordmark}>Tambra</Text>
          {app.libraryCount > 0 && (
            <View style={styles.headerRight}>
              <Text style={styles.libraryBadge}>{app.libraryCount}</Text>
            </View>
          )}
        </View>

        {/* Full-screen play surface (Z-layer 0) */}
        <View style={styles.surfaceContainer}>
          <PlaySurface
            onNoteOn={app.onNoteOn}
            onNoteOff={app.onNoteOff}
            isPlaying={app.session.isPlaying}
            scopeSamples={app.scopeSamples}
            octaveOffset={app.octaveOffset}
            onTouchStart={app.onPlayTouchStart}
            onTouchEnd={app.onPlayTouchEnd}
          />

          {/* Chat overlay (Z-layer 1) */}
          {isChatVisible && (
            <Animated.View
              style={[styles.chatOverlay, { opacity: app.chatOpacity }]}
              pointerEvents={app.chatOpacity < 0.5 ? 'none' : 'auto'}
            >
              <ChatThread
                messages={app.session.messages}
                activePatchCardId={app.session.activePatchCardId}
                isGenerating={app.session.isGenerating}
                opacity={1}
                onMacroChange={app.onMacroChange}
                onActivatePatch={app.onActivatePatch}
                onSavePatch={app.onSavePatch}
                onDismiss={() => setIsChatVisible(false)}
              />
            </Animated.View>
          )}
        </View>

        {/* Sound Shaper Controls (Z-layer 2) */}
        {!isChatVisible && (
          <SoundShaper
            macros={app.activeMacros}
            onMacroChange={app.onGlobalMacroChange}
            octaveOffset={app.octaveOffset}
            onOctaveChange={app.setOctaveOffset}
            isVisible={isShaperVisible}
            onToggleVisible={() => setIsShaperVisible((v) => !v)}
          />
        )}

        {/* Prompt bar (Z-layer 3) */}
        <KeyboardAvoidingView
          behavior={Platform.OS === 'ios' ? 'padding' : 'height'}
          keyboardVerticalOffset={0}
        >
          <Animated.View style={{ opacity: app.chatOpacity }}>
            <PromptBar
              onFocus={() => setIsChatVisible(true)}
              onSend={(text) => {
                setIsChatVisible(true);
                app.sendPrompt(text);
              }}
              onMicPress={app.sayCapture.tapRecord}
              isRecording={app.sayCapture.isRecording}
              micDisabled={app.sayCapture.micDisabled}
              isGenerating={app.session.isGenerating}
              opacity={1}
            />
          </Animated.View>
        </KeyboardAvoidingView>
      </View>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {
    flex: 1,
    backgroundColor: colors.bg.canvas,
  },
  root: {
    flex: 1,
  },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    paddingVertical: space['2'],
    paddingHorizontal: space.chromePadX,
    position: 'relative',
  },
  wordmark: {
    color: colors.accent.primary,
    fontSize: 22,
    fontWeight: '700',
    letterSpacing: 1.5,
    textTransform: 'uppercase',
  },
  headerRight: {
    position: 'absolute',
    right: space.chromePadX,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space['2'],
  },
  libraryBadge: {
    color: colors.text.secondary,
    fontSize: 12,
    fontWeight: '600',
  },
  surfaceContainer: {
    flex: 1,
    position: 'relative',
  },
  chatOverlay: {
    position: 'absolute',
    bottom: 0,
    left: 0,
    right: 0,
    height: '48%',
    paddingHorizontal: space.chromePadX,
  },
});
