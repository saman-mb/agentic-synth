import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, existsSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import {
  CORPUS,
  EVENT_SIZE,
  FRAMES,
  PATCH_STRUCT_SIZE,
  PCM_BYTES,
  SAMPLE_RATE,
  TOLERANCES,
} from "./corpus.mjs";

const here = dirname(fileURLToPath(import.meta.url));

describe("golden corpus layout", () => {
  it("lists 12 named patches covering the required topologies", () => {
    assert.equal(CORPUS.length, 12);
    const ids = CORPUS.map((c) => c.id);
    for (const id of [
      "sine",
      "tri",
      "saw",
      "square",
      "pulse",
      "wavetable",
      "fm",
      "noise",
      "lp-filter-env",
      "lfo-pitch-cutoff",
      "delay-reverb-wet",
      "acid-303-like",
    ]) {
      assert.ok(ids.includes(id), `missing ${id}`);
    }
  });

  it("gives sine an extra note-on at sample 10000; others share the default pair", () => {
    const sine = CORPUS.find((c) => c.id === "sine");
    assert.ok(sine);
    assert.deepEqual(
      sine.events.map((e) => [e.kind, e.note, e.velocity, e.sample_offset]),
      [
        [1, 60, 100, 0],
        [1, 60, 100, 10000],
        [2, 60, 0, 22050],
      ],
    );
    for (const c of CORPUS.filter((x) => x.id !== "sine")) {
      assert.deepEqual(
        c.events.map((e) => [e.kind, e.note, e.velocity, e.sample_offset]),
        [
          [1, 60, 100, 0],
          [2, 60, 0, 22050],
        ],
      );
    }
  });

  it("keeps JSON patches, packed ABI, and manifest in sync", () => {
    const manifest = JSON.parse(readFileSync(join(here, "manifest.json"), "utf8"));
    assert.equal(manifest.sample_rate, SAMPLE_RATE);
    assert.equal(manifest.frames, FRAMES);
    assert.equal(manifest.patch_struct_size, PATCH_STRUCT_SIZE);
    assert.equal(manifest.event_size, EVENT_SIZE);
    assert.deepEqual(manifest.tolerances, TOLERANCES);
    assert.equal(manifest.patches.length, 12);
    assert.match(manifest.graph, /VoiceManager/);
    assert.match(manifest.engine, /not WebSynthEngine/);

    for (const c of CORPUS) {
      const jsonPath = join(here, "patches", `${c.id}.json`);
      assert.ok(existsSync(jsonPath), jsonPath);
      const patch = JSON.parse(readFileSync(jsonPath, "utf8"));
      assert.equal(patch.osc.length, 3);
      assert.equal(typeof patch.filter.cutoff_hz, "number");

      const patchBin = join(here, "ref", `${c.id}.patch.bin`);
      const eventsBin = join(here, "ref", `${c.id}.events.bin`);
      assert.ok(existsSync(patchBin), patchBin);
      assert.ok(existsSync(eventsBin), eventsBin);
      assert.equal(statSync(patchBin).size, PATCH_STRUCT_SIZE);
      assert.equal(statSync(eventsBin).size, c.events.length * EVENT_SIZE);

      const entry = manifest.patches.find((p) => p.id === c.id);
      assert.ok(entry);
      assert.equal(entry.patch, `ref/${c.id}.patch.bin`);
      assert.equal(entry.events, `ref/${c.id}.events.bin`);
    }
  });

  it("documents a priori WebAudio bounds in the README", () => {
    const readme = readFileSync(join(here, "README.md"), "utf8");
    assert.match(readme, /memcmp/);
    assert.match(readme, /std::sin/);
    assert.match(readme, /1e-3/);
    assert.match(readme, /0\.25,\s*4|\[0\.25,\s*4\]/);
    assert.match(readme, /0\.85/);
    assert.match(readme, /1\.6/);
    assert.match(readme, /2\.5/);
    assert.match(readme, /PolyBLEP/);
    assert.match(readme, /OscillatorNode/);
    assert.match(readme, /PeriodicWave/);
    assert.match(readme, /mipmap/i);
    assert.match(readme, /RMS of each bucket/);
    assert.match(readme, /10-D RMS/);
    assert.doesNotMatch(readme, /raw sample vectors/);
    assert.doesNotMatch(readme, /minimum across buckets/i);
    assert.doesNotMatch(readme, /tuned to pass/);
    assert.doesNotMatch(readme, /we saw a fail/i);
  });

  it("keeps PeriodicWave/FM/triangle error-energy at 2.5 and the rest at 1.6", () => {
    assert.equal(TOLERANCES.wasm_native, "memcmp");
    assert.equal(TOLERANCES.wasm_native_fm_peak_abs, 1e-3);
    assert.equal(TOLERANCES.webaudio.err_rms_ratio, 1.6);
    assert.deepEqual(TOLERANCES.webaudio.err_rms_ratio_by_id, {
      pulse: 2.5,
      tri: 2.5,
      wavetable: 2.5,
      fm: 2.5,
    });
    for (const id of ["sine", "saw", "square", "noise", "lp-filter-env", "lfo-pitch-cutoff", "delay-reverb-wet", "acid-303-like"]) {
      assert.equal(TOLERANCES.webaudio.err_rms_ratio_by_id[id], undefined, id);
    }
  });

  it("does not gitignore f32le refs; marks them binary", () => {
    const gitignore = readFileSync(join(here, "..", "..", ".gitignore"), "utf8");
    assert.doesNotMatch(gitignore, /f32le/);
    const gitattributes = readFileSync(join(here, "..", "..", ".gitattributes"), "utf8");
    assert.match(gitattributes, /\*\.f32le binary/);
  });

  it("PCM refs are either absent or exactly 44100×2×f32", () => {
    for (const c of CORPUS) {
      const pcm = join(here, "ref", `${c.id}.f32le`);
      if (!existsSync(pcm)) continue;
      assert.equal(statSync(pcm).size, PCM_BYTES, c.id);
    }
  });

  it("delay+reverb wet is not a dry-saw clone when PCM exists", () => {
    const saw = join(here, "ref", "saw.f32le");
    const wet = join(here, "ref", "delay-reverb-wet.f32le");
    if (!existsSync(saw) || !existsSync(wet)) return;
    const a = readFileSync(saw);
    const b = readFileSync(wet);
    assert.notEqual(a.equals(b), true);
  });
});
