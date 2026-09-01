// WASM vs committed goldens + optional native dumps (#307).
//
// Loads dist/wasm/agsynth.js (EXPORT_ES6 createAgsynthModule), renders each
// corpus fixture through _ags_render_offline on the HEAP, then:
//   1. memcmp vs ref/<id>.f32le (WebAudio). Identity first; documented
//      RMS/bucket envelope only if bytes differ (topological mismatch).
//   2. If --native-dir is set, memcmp vs that dump. No RMS fallback.
//
// Missing dist/wasm is a hard fail — never a silent pass.

import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { dirname, join, parse, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const AGS_OK = 0;
const CHANNELS = 2;
const DEFAULT_SR = 44100;
const BUCKETS = 10;
const RMS_RATIO_MIN = 0.25;
const RMS_RATIO_MAX = 4;
const BUCKET_COSINE_MIN = 0.85;
const ERR_RMS_RATIO_MAX = 1.6;
const PEAK_ERR_MAX = 2;
// 2-op FM calls std::sin per sample (VoiceManager.cpp). glibc (CI native)
// vs emscripten's libm is not bit-identical. 1e-3 is ~60 dB below FS —
// far above a few ULPs, far below the WebAudio peak bound of 2. Recorded
// reason to relax memcmp for fixture id `fm` only (RFC §9).
const WASM_NATIVE_FM_PEAK = 1e-3;

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');
const wasmDir = join(repoRoot, 'dist', 'wasm');
const gluePath = join(wasmDir, 'agsynth.js');
const wasmPath = join(wasmDir, 'agsynth.wasm');
const refDir = join(here, 'ref');

function parseArgs(argv) {
  let nativeDir = null;
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--native-dir') {
      const value = argv[i + 1];
      if (!value || value.startsWith('-')) {
        console.error('compare-wasm: --native-dir requires a directory');
        process.exit(2);
      }
      nativeDir = value;
      i++;
      continue;
    }
    if (a.startsWith('--native-dir=')) {
      nativeDir = a.slice('--native-dir='.length);
      continue;
    }
    if (a === '--help' || a === '-h') {
      console.log('Usage: node tests/golden/compare-wasm.mjs [--native-dir DIR]');
      process.exit(0);
    }
    console.error(`compare-wasm: unknown argument ${a}`);
    process.exit(2);
  }
  return { nativeDir };
}

function failMissingWasm() {
  const missing = !existsSync(gluePath) ? gluePath : wasmPath;
  console.error(`compare-wasm: missing WASM artefact ${missing}`);
  console.error('Build it first: npx nx run wasm:build-wasm');
  console.error('Refusing to pass without a real module.');
  process.exit(1);
}

function listFixtures() {
  if (!existsSync(refDir)) return [];
  return readdirSync(refDir)
    .filter((name) => name.endsWith('.f32le'))
    .map((name) => parse(name).name)
    .sort();
}

function loadManifest() {
  const p = join(here, 'manifest.json');
  if (!existsSync(p)) return {};
  try {
    return JSON.parse(readFileSync(p, 'utf8'));
  } catch (err) {
    console.error(`compare-wasm: invalid manifest.json: ${err.message}`);
    process.exit(1);
  }
}

function sampleRateFor(id, manifest, f32le) {
  const fixtures = Array.isArray(manifest.fixtures) ? manifest.fixtures : [];
  const row = fixtures.find((f) => f && f.id === id);
  if (row && Number.isFinite(row.sr)) return row.sr;
  if (row && Number.isFinite(row.sample_rate)) return row.sample_rate;
  if (Number.isFinite(manifest.sr)) return manifest.sr;
  if (Number.isFinite(manifest.sample_rate)) return manifest.sample_rate;
  if (f32le.byteLength === DEFAULT_SR * CHANNELS * 4) return DEFAULT_SR;
  return DEFAULT_SR;
}

function asF32(buf) {
  if (buf.byteOffset % 4 === 0) {
    return new Float32Array(buf.buffer, buf.byteOffset, buf.byteLength / 4);
  }
  const copy = Buffer.from(buf);
  return new Float32Array(copy.buffer, copy.byteOffset, copy.byteLength / 4);
}

function isFiniteAll(samples) {
  for (let i = 0; i < samples.length; i++) {
    if (!Number.isFinite(samples[i])) return false;
  }
  return true;
}

function rms(samples) {
  if (samples.length === 0) return 0;
  let s = 0;
  for (let i = 0; i < samples.length; i++) s += samples[i] * samples[i];
  return Math.sqrt(s / samples.length);
}

function peakAbsErr(a, b) {
  let peak = 0;
  for (let i = 0; i < a.length; i++) {
    const e = Math.abs(a[i] - b[i]);
    if (e > peak) peak = e;
  }
  return peak;
}

function errRms(a, b) {
  let s = 0;
  for (let i = 0; i < a.length; i++) {
    const e = a[i] - b[i];
    s += e * e;
  }
  return Math.sqrt(s / a.length);
}

function rmsRange(samples, start, end) {
  const n = end - start;
  if (n <= 0) return 0;
  let s = 0;
  for (let i = start; i < end; i++) s += samples[i] * samples[i];
  return Math.sqrt(s / n);
}

// Cosine of 10 per-bucket RMS values (energy envelope), matching C++
// `bucket_cosine` in GoldenParityTest.cpp. Silent/silent → 1.
function bucketCosine(a, b) {
  if (a.length === 0) return 0;
  const n = a.length;
  const base = Math.floor(n / BUCKETS);
  const bx = new Array(BUCKETS);
  const br = new Array(BUCKETS);
  for (let k = 0; k < BUCKETS; k++) {
    const start = k * base;
    const end = k === BUCKETS - 1 ? n : start + base;
    bx[k] = rmsRange(a, start, end);
    br[k] = rmsRange(b, start, end);
  }
  let dot = 0;
  let nx = 0;
  let nr = 0;
  for (let k = 0; k < BUCKETS; k++) {
    dot += bx[k] * br[k];
    nx += bx[k] * bx[k];
    nr += br[k] * br[k];
  }
  if (nx === 0 && nr === 0) return 1;
  if (nx === 0 || nr === 0) return 0;
  return dot / (Math.sqrt(nx) * Math.sqrt(nr));
}

function nativeDiffStats(a, b) {
  const fa = asF32(a);
  const fb = asF32(b);
  let nDiff = 0;
  let peak = 0;
  let errSq = 0;
  const n = Math.min(fa.length, fb.length);
  for (let i = 0; i < n; i++) {
    const e = Math.abs(fa[i] - fb[i]);
    if (e > 0) nDiff += 1;
    if (e > peak) peak = e;
    errSq += e * e;
  }
  return { nDiff, peak, rms: n === 0 ? 0 : Math.sqrt(errSq / n) };
}

function errRmsMaxFor(id, manifest) {
  const webaudio = manifest?.tolerances?.webaudio;
  const byId = webaudio?.err_rms_ratio_by_id;
  if (byId && Number.isFinite(byId[id]) && byId[id] > 0) return byId[id];
  if (Number.isFinite(webaudio?.err_rms_ratio) && webaudio.err_rms_ratio > 0) {
    return webaudio.err_rms_ratio;
  }
  return ERR_RMS_RATIO_MAX;
}

function webaudioEnvelope(got, ref, errRmsMax = ERR_RMS_RATIO_MAX) {
  if (got.length !== ref.length) {
    return `length ${got.length} != ref ${ref.length}`;
  }
  if (!isFiniteAll(got)) return 'NaN/Inf in WASM output';
  if (!isFiniteAll(ref)) return 'NaN/Inf in WebAudio ref';
  const rmsGot = rms(got);
  const rmsRef = rms(ref);
  if (rmsRef === 0) {
    return rmsGot === 0 ? null : 'ref silent but WASM is not';
  }
  const ratio = rmsGot / rmsRef;
  if (ratio < RMS_RATIO_MIN || ratio > RMS_RATIO_MAX) {
    return `RMS ratio ${ratio.toFixed(4)} not in [${RMS_RATIO_MIN}, ${RMS_RATIO_MAX}]`;
  }
  const cos = bucketCosine(got, ref);
  if (cos < BUCKET_COSINE_MIN) {
    return `bucket cosine ${cos.toFixed(4)} < ${BUCKET_COSINE_MIN}`;
  }
  const errRatio = errRms(got, ref) / rmsRef;
  if (errRatio > errRmsMax) {
    return `RMS(err)/RMS(ref) ${errRatio.toFixed(4)} > ${errRmsMax}`;
  }
  const peak = peakAbsErr(got, ref);
  if (peak > PEAK_ERR_MAX) {
    return `peak |err| ${peak.toFixed(4)} > ${PEAK_ERR_MAX}`;
  }
  return null;
}

function mallocCopy(mod, bytes) {
  if (!bytes || bytes.byteLength === 0) return 0;
  const ptr = mod._malloc(bytes.byteLength);
  if (!ptr) throw new Error('malloc failed');
  mod.HEAPU8.set(bytes, ptr);
  return ptr;
}

async function loadModule() {
  const glue = await import(pathToFileURL(gluePath).href);
  const createAgsynthModule = glue.default ?? glue.createAgsynthModule;
  if (typeof createAgsynthModule !== 'function') {
    console.error('compare-wasm: createAgsynthModule missing from dist/wasm/agsynth.js');
    process.exit(1);
  }
  return createAgsynthModule({
    locateFile(file) {
      return join(wasmDir, file);
    },
  });
}

function renderOffline(mod, patch, events, sampleRate, frames) {
  const outBytes = frames * CHANNELS * 4;
  const patchPtr = mallocCopy(mod, patch);
  const eventsPtr = mallocCopy(mod, events);
  const outPtr = mod._malloc(outBytes);
  if (!outPtr) throw new Error('malloc failed for render buffer');
  try {
    const rc = mod._ags_render_offline(
      patchPtr,
      patch.byteLength,
      eventsPtr,
      events.byteLength / 12,
      sampleRate,
      frames,
      outPtr,
    );
    if (rc !== AGS_OK) {
      throw new Error(`_ags_render_offline returned ${rc}`);
    }
    return Buffer.from(mod.HEAPU8.subarray(outPtr, outPtr + outBytes));
  } finally {
    if (patchPtr) mod._free(patchPtr);
    if (eventsPtr) mod._free(eventsPtr);
    mod._free(outPtr);
  }
}

function compareFixture(id, wasmBytes, refBytes, nativeBytes, errRmsMax) {
  const errors = [];
  if (wasmBytes.byteLength !== refBytes.byteLength) {
    errors.push(
      `WASM ${wasmBytes.byteLength} bytes != ref ${refBytes.byteLength} (same length required)`,
    );
    return errors;
  }

  const memcmpRef = Buffer.compare(wasmBytes, refBytes) === 0;
  if (memcmpRef) {
    process.stdout.write(`  ${id}: memcmp vs ref/*.f32le OK\n`);
  } else {
    const envErr = webaudioEnvelope(asF32(wasmBytes), asF32(refBytes), errRmsMax);
    if (envErr) {
      errors.push(`${id}: memcmp vs WebAudio failed, envelope: ${envErr}`);
    } else {
      process.stdout.write(
        `  ${id}: memcmp vs ref/*.f32le miss (expected vs WebAudio); RMS/bucket OK\n`,
      );
    }
  }

  if (nativeBytes) {
    if (nativeBytes.byteLength !== wasmBytes.byteLength) {
      errors.push(
        `${id}: native dump ${nativeBytes.byteLength} bytes != WASM ${wasmBytes.byteLength}`,
      );
    } else if (Buffer.compare(wasmBytes, nativeBytes) !== 0) {
      const stats = nativeDiffStats(wasmBytes, nativeBytes);
      const summary = `n_diff=${stats.nDiff} peak=${stats.peak} rms=${stats.rms}`;
      process.stdout.write(`  ${id}: memcmp vs --native-dir miss (${summary})\n`);
      if (id === 'fm' && stats.peak <= WASM_NATIVE_FM_PEAK) {
        process.stdout.write(
          `  ${id}: wasm↔native libm sin bound OK (peak ≤ ${WASM_NATIVE_FM_PEAK})\n`,
        );
      } else {
        errors.push(
          `${id}: memcmp vs --native-dir dump failed (WASM↔native must be bit-identical) ${summary}`,
        );
      }
    } else {
      process.stdout.write(`  ${id}: memcmp vs --native-dir OK\n`);
    }
  }

  return errors;
}

const { nativeDir } = parseArgs(process.argv.slice(2));

if (!existsSync(gluePath) || !existsSync(wasmPath)) {
  failMissingWasm();
}

const ids = listFixtures();
if (ids.length === 0) {
  console.error(`compare-wasm: no committed goldens at ${join(refDir, '*.f32le')}`);
  console.error('Generate and commit the corpus (tests/golden/generate.mjs) first.');
  process.exit(1);
}

const manifest = loadManifest();
let mod;
try {
  mod = await loadModule();
} catch (err) {
  const message = err instanceof Error ? err.message : String(err);
  console.error(`compare-wasm: failed to instantiate WASM module: ${message}`);
  process.exit(1);
}
if (typeof mod._ags_render_offline !== 'function') {
  console.error('compare-wasm: _ags_render_offline is not exported from the WASM module');
  process.exit(1);
}

const failures = [];
for (const id of ids) {
  const refPath = join(refDir, `${id}.f32le`);
  const patchPath = join(refDir, `${id}.patch.bin`);
  const eventsPath = join(refDir, `${id}.events.bin`);
  if (!existsSync(patchPath) || !existsSync(eventsPath)) {
    failures.push(`${id}: missing ${id}.patch.bin or ${id}.events.bin beside the f32le`);
    continue;
  }
  const refBytes = readFileSync(refPath);
  if (refBytes.byteLength === 0 || refBytes.byteLength % (CHANNELS * 4) !== 0) {
    failures.push(`${id}: invalid f32le size ${refBytes.byteLength}`);
    continue;
  }
  const frames = refBytes.byteLength / (CHANNELS * 4);
  const sr = sampleRateFor(id, manifest, refBytes);
  const patch = readFileSync(patchPath);
  const events = readFileSync(eventsPath);
  if (events.byteLength % 12 !== 0) {
    failures.push(`${id}: events.bin length ${events.byteLength} is not a multiple of 12`);
    continue;
  }

  let nativeBytes = null;
  if (nativeDir) {
    const dumpPath = join(nativeDir, `${id}.f32le`);
    if (!existsSync(dumpPath)) {
      failures.push(`${id}: --native-dir dump missing at ${dumpPath}`);
      continue;
    }
    nativeBytes = readFileSync(dumpPath);
  }

  let wasmBytes;
  try {
    wasmBytes = renderOffline(mod, patch, events, sr, frames);
  } catch (err) {
    failures.push(`${id}: render failed: ${err instanceof Error ? err.message : String(err)}`);
    continue;
  }
  failures.push(...compareFixture(id, wasmBytes, refBytes, nativeBytes, errRmsMaxFor(id, manifest)));
}

if (failures.length > 0) {
  console.error('compare-wasm: FAIL');
  for (const line of failures) console.error(`  ${line}`);
  process.exit(1);
}

console.log(`compare-wasm: OK (${ids.length} fixture${ids.length === 1 ? '' : 's'})`);
