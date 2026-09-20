import { StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { BrandHeader } from '../src/components/BrandHeader';
import { BottomSheet } from '../src/components/BottomSheet';
import { InputCta } from '../src/components/InputCta';
import { MacroKnobs } from '../src/components/MacroKnobs';
import { PlayButton } from '../src/components/PlayButton';
import { StatusLine } from '../src/components/StatusLine';
import { Visualizer } from '../src/components/Visualizer';
import { useMobileApp } from '../src/hooks/useMobileApp';
import { colors, space } from '../src/theme/tokens';

export default function HomeScreen() {
  const {
    session,
    scratch,
    libraryCount,
    backend,
    scopeSamples,
    togglePlay,
    openSay,
    cancelSay,
    sendPrompt,
    sayCapture,
    generating,
    macroKnobs,
    swipeVariation,
    openVariations,
    backToShape,
    selectVariation,
    requestMoreVariations,
    openKeep,
    cancelKeep,
    confirmKeep,
    setKeepName,
    regenerate,
    variationLoading,
    keeping,
    cancelGenerate,
    dismissError,
    retryFromError,
    openLibrary,
  } = useMobileApp();

  const shapeActive = session.state === 'shape' || session.state === 'variations';
  const canSwipe =
    shapeActive && scratch.prompt.length > 0 && !macroKnobs.isDragging();

  return (
    <SafeAreaView style={styles.safe} edges={['top', 'left', 'right']}>
      <View style={styles.chrome}>
        <BrandHeader libraryCount={libraryCount} onLibraryPress={openLibrary} />
        <Visualizer
          isPlaying={session.isPlaying}
          scopeSamples={scopeSamples}
          swipeEnabled={canSwipe}
          onSwipe={(dir) => void swipeVariation(dir)}
        />
        <MacroKnobs
          active={shapeActive}
          positions={macroKnobs.positions}
          onChange={macroKnobs.setMacro}
          onDragStart={macroKnobs.onDragStart}
          onDragEnd={macroKnobs.onDragEnd}
        />
        <View style={styles.controls}>
          <PlayButton isPlaying={session.isPlaying} onPress={togglePlay} />
          <InputCta onPress={openSay} />
        </View>
        <StatusLine message={session.statusMessage} backend={backend} />
      </View>
      <BottomSheet
        state={session.state}
        say={{
          capture: sayCapture,
          onSend: () => void sendPrompt(),
          onCancel: cancelSay,
          generating,
        }}
        hear={{
          message: session.statusMessage || 'Building your sound…',
          onCancel: generating ? cancelGenerate : undefined,
        }}
        shape={{
          promptEcho: scratch.prompt,
          onVariations: openVariations,
          onKeep: openKeep,
          onRegenerate: () => void regenerate(),
          onNewIdea: openSay,
        }}
        variations={{
          items: scratch.variations,
          selectedIndex: scratch.selectedVariationIndex,
          loading: variationLoading,
          onSelect: (i) => void selectVariation(i),
          onMore: () => void requestMoreVariations(),
          onBack: backToShape,
          onKeep: openKeep,
        }}
        keep={{
          name: scratch.keepNameDraft,
          onNameChange: setKeepName,
          onConfirm: () => void confirmKeep(),
          onCancel: cancelKeep,
          saving: keeping,
        }}
        error={{
          message: session.statusMessage,
          onRetry: session.returnState ? retryFromError : undefined,
          onDismiss: dismissError,
        }}
      />
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {
    flex: 1,
    backgroundColor: colors.bg.canvas,
  },
  chrome: {
    flex: 1,
    paddingHorizontal: space.chromePadX,
    paddingTop: space.chromePadY,
  },
  controls: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: space['6'],
    marginTop: space['5'],
  },
});
