import { StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { useBootEngine } from '../hooks/useBootEngine';
import { colors, space } from '../theme/tokens';

import { BottomSheet } from './BottomSheet';
import { BrandHeader } from './BrandHeader';
import { InputCta } from './InputCta';
import { MacroKnobs } from './MacroKnobs';
import { StatusLine } from './StatusLine';
import { Visualizer } from './Visualizer';

export function ChromeScreen() {
  const { state, statusMessage, isPlaying, backend, scopeSamples } = useBootEngine();

  return (
    <SafeAreaView style={styles.root} edges={['top', 'bottom']}>
      <View style={styles.chrome}>
        <BrandHeader />
        <Visualizer isPlaying={isPlaying} scopeSamples={scopeSamples} />
        <MacroKnobs />
        <InputCta />
        <StatusLine message={statusMessage} backend={backend ?? undefined} />
      </View>
      <BottomSheet state={state} />
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: colors.bg.void,
    justifyContent: 'space-between',
  },
  chrome: {
    flex: 1,
    paddingHorizontal: space.chromePadX,
  },
});
