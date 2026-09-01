#pragma once

// Install API for the TypeScript sibling (`libs/engine-bridge/src/lib/jsiEngine.ts`).
//
// Build the HostObject with -DAGENTIC_SYNTH_HAS_JSI=ON and React Native / JSI
// headers on the include path. Do not vendor React Native into this repo.
// Linux CI leaves AGENTIC_SYNTH_HAS_JSI OFF (no RN headers).
//
//   namespace agentic_synth::jsi {
//     void installAgsynthHost(facebook::jsi::Runtime& runtime);
//   }
//
// After install, `global.__AgsynthHost` is a HostObject:
//   create(sampleRate: number, maxBlock?: number): number
//   destroy(): number
//   setPatch(bytes: ArrayBuffer): number
//   setParam(path: string, value: number): number
//   pushEvents(bytes: ArrayBuffer): number   // packed ags_event[]
//   processBlock(out: ArrayBuffer, frames: number, channels: number): number
//     AGS_ERR_STATE if AudioStream is running (RT owns processBlock).
//     AGS_ERR_SIZE if out.byteLength < frames*channels*sizeof(float).
//   renderOffline(patch: ArrayBuffer, events: ArrayBuffer,
//                 sampleRate: number, frames: number, out: ArrayBuffer): number
//     Stereo (2 ch). AGS_ERR_SIZE if out.byteLength < frames*2*sizeof(float).
//   start(): number
//   stop(): number
//   stateSize(): number
//   saveState(out: ArrayBuffer): number
//   loadState(bytes: ArrayBuffer): number
//   recreate(sampleRate: number, maxBlock?: number): number
//
// Numeric codes (native never throws across the JSI boundary):
//   AGS_OK=0, AGS_ERR_PARAM=1, AGS_ERR_SIZE=2, AGS_ERR_STATE=3,
//   AGS_ERR_NULL=4, AGS_JSI_ERR_QUEUE=5.
// TS maps those to AgsynthError { code: 'PARAM'|'SIZE'|'STATE'|'NULL'|'QUEUE' }.

#ifdef AGENTIC_SYNTH_HAS_JSI

#include <jsi/jsi.h>

namespace agentic_synth::jsi {

void installAgsynthHost(facebook::jsi::Runtime& runtime);

} // namespace agentic_synth::jsi

#endif
