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
  const { session, backend, scopeSamples, togglePlay } = useMobileApp();

  return (
    <SafeAreaView style={styles.safe} edges={['top', 'left', 'right']}>
      <View style={styles.chrome}>
        <BrandHeader />
        <Visualizer isPlaying={session.isPlaying} scopeSamples={scopeSamples} />
        <MacroKnobs />
        <View style={styles.controls}>
          <PlayButton isPlaying={session.isPlaying} onPress={togglePlay} />
          <InputCta />
        </View>
        <StatusLine message={session.statusMessage} backend={backend} />
      </View>
      <BottomSheet state={session.state} />
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
