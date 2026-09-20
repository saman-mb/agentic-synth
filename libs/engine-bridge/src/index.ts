export { WebSynthEngine, WasmSynthEngine, type SynthEngine } from './lib/engine';
export {
  createSynthEngine,
  ResilientSynthEngine,
  probeWasmWorklet,
  type EngineKind,
} from './lib/resilientEngine';
export {
  createAudioContext,
  isWebAudioSupported,
  prepareAudioSession,
  keepAudioContextRunning,
} from './lib/audioEnvironment';
export { JsiSynthEngine, AgsynthError, type JsiNativeBinding } from './lib/jsiEngine';
export { packPatchParams, PATCH_STRUCT_SIZE } from './lib/patchAbi';
export { getPatchParam, setPatchParam } from './lib/paramMap';
export {
  SPECTRUM_FLOOR_DB,
  SPECTRUM_CEIL_DB,
  SPECTRUM_FFT_SIZE,
  dbToNorm,
  deinterleave,
  computeSpectrumBands,
  detectFundamental,
  emptyScopeFrame,
  frequencyToX,
  fftInPlace,
  lissajousPoint,
  parseScopeFrame,
  type ScopeFrame,
  type Fundamental,
} from './lib/scope';

