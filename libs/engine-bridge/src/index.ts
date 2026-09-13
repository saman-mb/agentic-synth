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
export { JsiSynthEngine, AgsynthError } from './lib/jsiEngine';
export { packPatchParams, PATCH_STRUCT_SIZE } from './lib/patchAbi';
export { setPatchParam } from './lib/paramMap';
