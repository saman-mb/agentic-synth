#!/usr/bin/env node
// ── Golden reference generator (#307) ────────────────────────────────
//
// Packs the corpus through packPatchParams, writes events in the
// ags_event ABI, and renders 1 s of stereo f32le from Chromium's
// OfflineAudioContext via VoiceManager + createEffectRack + GainNode.
// WebSynthEngine is not used (analyser / worklet / AudioContext).
//
//   node tests/golden/generate.mjs
//
// Playwright/Chromium missing: JSON patches, ABI bins, README inputs,
// and manifest are still written. PCM is never invented.

import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
  AGS_EVENT_NOTE_OFF,
  AGS_EVENT_NOTE_ON,
  CHANNELS,
  CORPUS,
  EVENT_SIZE,
  FRAMES,
  PATCH_STRUCT_SIZE,
  PCM_BYTES,
  SAMPLE_RATE,
  TOLERANCES,
} from "./corpus.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..", "..");
const refDir = join(here, "ref");
const patchesDir = join(here, "patches");
const require = createRequire(import.meta.url);

const IMPULSE_WAIT_MS = 100;
const RENDER_TIMEOUT_MS = 60_000;

function packEvents(events) {
  const buf = new ArrayBuffer(events.length * EVENT_SIZE);
  const view = new DataView(buf);
  for (let i = 0; i < events.length; i++) {
    const e = events[i];
    const o = i * EVENT_SIZE;
    view.setUint32(o, e.kind, true);
    view.setUint8(o + 4, e.note);
    view.setUint8(o + 5, e.velocity);
    view.setUint8(o + 6, e.cc ?? 0);
    view.setUint8(o + 7, 0);
    view.setUint32(o + 8, e.sample_offset, true);
  }
  return Buffer.from(buf);
}

function resolveEsbuild() {
  const candidates = ["esbuild", "vite/node_modules/esbuild"];
  for (const spec of candidates) {
    try {
      return require(spec);
    } catch {
      // try next
    }
  }
  throw new Error("esbuild not found (npm install at repo root — vite pulls it in)");
}

async function bundlePacker(esbuild, cacheDir) {
  const outfile = join(cacheDir, "pack.mjs");
  await esbuild.build({
    absWorkingDir: root,
    entryPoints: [join(root, "libs/engine-bridge/src/lib/patchAbi.ts")],
    bundle: true,
    format: "esm",
    platform: "neutral",
    outfile,
    logLevel: "silent",
  });
  return import(pathToFileURL(outfile).href);
}

async function bundleBrowser(esbuild, cacheDir) {
  const outfile = join(cacheDir, "render.iife.js");
  await esbuild.build({
    absWorkingDir: root,
    entryPoints: [join(here, "browser-entry.ts")],
    bundle: true,
    format: "iife",
    globalName: "__agsynthGolden",
    platform: "browser",
    outfile,
    logLevel: "silent",
    alias: {
      "@agentic-synth/shared-types": join(root, "libs/shared-types/src/index.ts"),
      "@agentic-synth/data": join(root, "libs/data/src/lib/paramRanges.ts"),
    },
  });
  return outfile;
}

function writeManifest(pcmById) {
  const patches = CORPUS.map((c) => {
    const pcmPath = `ref/${c.id}.f32le`;
    const generated = pcmById.has(c.id);
    return {
      id: c.id,
      title: c.title,
      pcm: generated ? pcmPath : null,
      patch: `ref/${c.id}.patch.bin`,
      events: `ref/${c.id}.events.bin`,
      json: `patches/${c.id}.json`,
      err_rms_ratio:
        TOLERANCES.webaudio.err_rms_ratio_by_id[c.id] ?? TOLERANCES.webaudio.err_rms_ratio,
      event_list: c.events,
    };
  });
  const manifest = {
    sample_rate: SAMPLE_RATE,
    frames: FRAMES,
    channels: CHANNELS,
    format: "f32le_interleaved",
    patch_struct_size: PATCH_STRUCT_SIZE,
    event_size: EVENT_SIZE,
    event_endian: "le",
    event_kinds: { NOTE_ON: AGS_EVENT_NOTE_ON, NOTE_OFF: AGS_EVENT_NOTE_OFF },
    graph: "VoiceManager → createEffectRack → GainNode → OfflineAudioContext.destination",
    engine: "WebAudio OfflineAudioContext (not WebSynthEngine)",
    tolerances: TOLERANCES,
    pcm_generated: [...pcmById.keys()],
    patches,
  };
  writeFileSync(join(here, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  return manifest;
}

function playwrightFromModule(m) {
  const chromium = m?.chromium ?? m?.default?.chromium;
  return chromium?.launch ? { chromium } : null;
}

function loadPlaywright() {
  try {
    return playwrightFromModule(require("playwright"));
  } catch {
    return null;
  }
}

function installPlaywrightOnce() {
  execFileSync("npm", ["install", "--no-save", "playwright"], {
    cwd: root,
    stdio: "inherit",
  });
  execFileSync("npx", ["--yes", "playwright", "install", "chromium"], {
    cwd: root,
    stdio: "inherit",
  });
}

const PAGE_RENDER_SOURCE = String.raw`
async function renderGolden(patch, events, opts) {
  const api = globalThis.__agsynthGolden;
  if (!api || !api.VoiceManager || !api.createEffectRack) {
    throw new Error("browser bundle missing VoiceManager/createEffectRack");
  }
  const sr = opts.sampleRate;
  const frames = opts.frames;
  const ctx = new OfflineAudioContext(opts.channels, frames, sr);

  // OfflineAudioContext drops setTargetAtTime scheduled at t=0 before
  // startRendering (Chromium). Snap .value during graph setup so LFO
  // depths and FX send mixes land; restore before noteOn so ADSR decay
  // still uses a real exponential.
  const proto = AudioParam.prototype;
  const origTarget = proto.setTargetAtTime;
  proto.setTargetAtTime = function (value, startTime, timeConstant) {
    this.value = value;
    return origTarget.call(this, value, startTime, timeConstant);
  };

  let manager;
  let rack;
  try {
    const holder = { patch };
    manager = new api.VoiceManager(ctx, () => holder.patch);
    rack = api.createEffectRack(ctx);
    const master = ctx.createGain();
    master.gain.value = Math.min(1, Math.max(0, patch.master_gain));
    manager.output.connect(rack.input);
    rack.output.connect(master);
    master.connect(ctx.destination);
    rack.setReverb(patch.reverb);
    rack.setDelay(patch.delay);
  } finally {
    proto.setTargetAtTime = origTarget;
  }
  await new Promise((r) => setTimeout(r, opts.impulseWaitMs));

  const apply = (ev) => {
    const vel = ev.velocity / 127;
    if (ev.kind === 1) manager.noteOn(ev.note, vel);
    else if (ev.kind === 2) manager.noteOff(ev.note);
  };

  const byOffset = new Map();
  for (const ev of events) {
    const off = ev.sample_offset | 0;
    if (!byOffset.has(off)) byOffset.set(off, []);
    byOffset.get(off).push(ev);
  }
  for (const ev of byOffset.get(0) || []) apply(ev);
  const later = [...byOffset.keys()].filter((o) => o > 0).sort((a, b) => a - b);
  for (const off of later) {
    const when = off / sr;
    ctx.suspend(when).then(() => {
      for (const ev of byOffset.get(off)) apply(ev);
      return ctx.resume();
    });
  }

  const rendered = await ctx.startRendering();
  const interleaved = new Float32Array(frames * opts.channels);
  const ch0 = rendered.getChannelData(0);
  const ch1 = rendered.numberOfChannels > 1 ? rendered.getChannelData(1) : ch0;
  for (let i = 0; i < frames; i++) {
    interleaved[i * 2] = ch0[i];
    interleaved[i * 2 + 1] = ch1[i];
  }
  manager.dispose();
  rack.dispose();
  return new Uint8Array(interleaved.buffer);
}
`;

async function renderAll(browserJs, pcmById) {
  let playwright = loadPlaywright();
  if (!playwright) {
    console.warn("playwright package missing — installing chromium once");
    try {
      installPlaywrightOnce();
    } catch (err) {
      throw new Error(`playwright install failed: ${err instanceof Error ? err.message : err}`);
    }
    playwright = loadPlaywright();
    if (!playwright) {
      try {
        playwright = playwrightFromModule(await import("playwright"));
      } catch {
        playwright = null;
      }
    }
    if (!playwright) throw new Error("playwright still missing after install");
  }

  const browser = await playwright.chromium.launch({
    headless: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage"],
  });
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(RENDER_TIMEOUT_MS);
    await page.setContent("<!doctype html><title>golden</title>");
    await page.addScriptTag({ path: browserJs });
    await page.addScriptTag({ content: PAGE_RENDER_SOURCE });

    for (const c of CORPUS) {
      const bytes = await page.evaluate(
        async ({ patch, events, opts }) => {
          const u8 = await renderGolden(patch, events, opts);
          // Playwright serializes Uint8Array as a JSON-like object; a
          // plain number[] round-trips as bytes.
          return Array.from(u8);
        },
        {
          patch: c.patch,
          events: c.events,
          opts: {
            sampleRate: SAMPLE_RATE,
            frames: FRAMES,
            channels: CHANNELS,
            impulseWaitMs: IMPULSE_WAIT_MS,
          },
        },
      );
      if (!Array.isArray(bytes) || bytes.length !== PCM_BYTES) {
        throw new Error(`${c.id}: expected ${PCM_BYTES} bytes, got ${Array.isArray(bytes) ? bytes.length : typeof bytes}`);
      }
      const buf = Buffer.from(bytes);
      writeFileSync(join(refDir, `${c.id}.f32le`), buf);
      pcmById.set(c.id, buf.byteLength);
      console.log(`rendered ${c.id} (${buf.byteLength} bytes)`);
    }
  } finally {
    await browser.close();
  }
}

async function main() {
  mkdirSync(refDir, { recursive: true });
  mkdirSync(patchesDir, { recursive: true });

  for (const c of CORPUS) {
    writeFileSync(join(patchesDir, `${c.id}.json`), JSON.stringify(c.patch, null, 2) + "\n");
  }

  const cacheDir = join(tmpdir(), "agsynth-golden");
  mkdirSync(cacheDir, { recursive: true });
  const esbuild = resolveEsbuild();
  const packer = await bundlePacker(esbuild, cacheDir);
  if (packer.PATCH_STRUCT_SIZE !== PATCH_STRUCT_SIZE) {
    throw new Error(`packPatchParams size ${packer.PATCH_STRUCT_SIZE} !== ${PATCH_STRUCT_SIZE}`);
  }

  for (const c of CORPUS) {
    const packed = packer.packPatchParams(c.patch);
    if (packed.byteLength !== PATCH_STRUCT_SIZE) {
      throw new Error(`${c.id}: packed patch is ${packed.byteLength} bytes`);
    }
    writeFileSync(join(refDir, `${c.id}.patch.bin`), Buffer.from(packed));
    writeFileSync(join(refDir, `${c.id}.events.bin`), packEvents(c.events));
  }

  const pcmById = new Map();
  writeManifest(pcmById);

  let browserJs;
  try {
    browserJs = await bundleBrowser(esbuild, cacheDir);
  } catch (err) {
    console.error(`browser bundle failed: ${err instanceof Error ? err.message : err}`);
    writeManifest(pcmById);
    process.exitCode = 1;
    return;
  }

  try {
    await renderAll(browserJs, pcmById);
  } catch (err) {
    console.error(`PCM render failed: ${err instanceof Error ? err.message : err}`);
    writeManifest(pcmById);
    process.exitCode = 1;
    return;
  }

  writeManifest(pcmById);
  console.log(`wrote ${CORPUS.length} patches, ${pcmById.size} PCM refs`);
}

main().catch((err) => {
  console.error(err);
  process.exitCode = 1;
});
