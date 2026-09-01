// ── Request body + generate assembly tests (#311) ───────────────────

import { describe, it } from "node:test";
import assert from "node:assert/strict";

import {
  MAX_REQUEST_BODY_BYTES,
  readJsonObject,
} from "./requestBody.mts";
import { assembleGeneratePayload } from "./assembleGeneratePayload.mts";
import { handleBrief } from "../brief.mts";
import { handleGenerate } from "../generate.mts";

function validLlmPatchJson(overrides: Record<string, unknown> = {}): string {
  const patch = {
    version: 1,
    patch_id: 1,
    osc: [
      {
        type: "Sawtooth",
        semitone_offset: 0,
        detune_cents: 0,
        wavetable_pos: 0,
        fm_ratio: 1,
        fm_depth: 0,
        volume: 0.8,
        pan: 0,
        pulse_width: 0.5,
        enabled: true,
      },
      {
        type: "Sine",
        semitone_offset: 0,
        detune_cents: 0,
        wavetable_pos: 0,
        fm_ratio: 1,
        fm_depth: 0,
        volume: 0,
        pan: 0,
        pulse_width: 0.5,
        enabled: false,
      },
      {
        type: "Sine",
        semitone_offset: 0,
        detune_cents: 0,
        wavetable_pos: 0,
        fm_ratio: 1,
        fm_depth: 0,
        volume: 0,
        pan: 0,
        pulse_width: 0.5,
        enabled: false,
      },
    ],
    filter: {
      type: "LowPass",
      cutoff_hz: 1000,
      resonance: 0.2,
      env_mod: 0,
      key_track: 0,
      drive: 0,
    },
    filter_env: { attack_s: 0.01, decay_s: 0.2, sustain: 0, release_s: 0.1 },
    amp_env: { attack_s: 0.005, decay_s: 0.1, sustain: 1, release_s: 0.1 },
    lfo: [
      {
        waveform: "Sine",
        target: "None",
        rate_hz: 1,
        depth: 0,
        phase_offset: 0,
        bpm_sync: false,
      },
      {
        waveform: "Sine",
        target: "None",
        rate_hz: 1,
        depth: 0,
        phase_offset: 0,
        bpm_sync: false,
      },
    ],
    reverb: { size: 0.5, damping: 0.5, width: 1, mix: 0 },
    delay: { time_s: 0.25, feedback: 0.3, mix: 0, stereo: 0.5, bpm_sync: false },
    master_gain: 0.8,
    portamento_s: 0,
    voice_count: 8,
    ...overrides,
  };
  return JSON.stringify(patch);
}

describe("readJsonObject", () => {
  it("parses a small JSON object", async () => {
    const req = new Request("http://local/api/brief", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ prompt: "hi" }),
    });
    const r = await readJsonObject(req);
    assert.equal(r.ok, true);
    if (r.ok) assert.deepEqual(r.value, { prompt: "hi" });
  });

  it("rejects Content-Length over the shared cap with 400-shaped error", async () => {
    const req = new Request("http://local/api/brief", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Content-Length": String(MAX_REQUEST_BODY_BYTES + 1),
      },
      body: "{}",
    });
    const r = await readJsonObject(req);
    assert.equal(r.ok, false);
    if (!r.ok) {
      assert.match(r.error, /at most/);
    }
  });

  it("rejects an oversized body after read", async () => {
    const huge = `{"prompt":"${"x".repeat(MAX_REQUEST_BODY_BYTES)}}`;
    assert.ok(new TextEncoder().encode(huge).byteLength > MAX_REQUEST_BODY_BYTES);
    const req = new Request("http://local/api/brief", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: huge,
    });
    const r = await readJsonObject(req);
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /at most/);
  });

  it("rejects malformed JSON", async () => {
    const req = new Request("http://local/api/brief", {
      method: "POST",
      body: "{not-json",
    });
    const r = await readJsonObject(req);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.error, "Request body must be valid JSON.");
  });

  it("rejects JSON arrays", async () => {
    const req = new Request("http://local/api/brief", {
      method: "POST",
      body: "[1,2]",
    });
    const r = await readJsonObject(req);
    assert.equal(r.ok, false);
    if (!r.ok) assert.equal(r.error, "Request body must be a JSON object.");
  });
});

describe("assembleGeneratePayload", () => {
  it("accepts a valid LLM patch via shared codec", () => {
    const r = assembleGeneratePayload(validLlmPatchJson(), "brief text");
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.payload["brief"], "brief text");
      assert.ok(r.payload["patch"]);
    }
  });

  it("returns 502 (not 500) for malformed model JSON", () => {
    const r = assembleGeneratePayload("{nope", "brief");
    assert.equal(r.ok, false);
    if (!r.ok) {
      assert.equal(r.status, 502);
      assert.match(r.error, /malformed JSON/);
    }
  });

  it("returns 502 for unknown version / out-of-spec shapes", () => {
    const badVersion = assembleGeneratePayload(
      validLlmPatchJson({ version: 99 }),
      "brief",
    );
    assert.equal(badVersion.ok, false);
    if (!badVersion.ok) assert.equal(badVersion.status, 502);

    const badShape = assembleGeneratePayload(
      JSON.stringify({ version: 1, osc: [] }),
      "brief",
    );
    assert.equal(badShape.ok, false);
    if (!badShape.ok) assert.equal(badShape.status, 502);
  });

  it("returns 502 for invalid modulation via shared modval", () => {
    const r = assembleGeneratePayload(
      validLlmPatchJson({
        modulation: {
          macros: [{ routes: [{ target: "nope", amount: 2 }] }],
        },
      }),
      "brief",
    );
    assert.equal(r.ok, false);
    if (!r.ok) {
      assert.equal(r.status, 502);
      assert.match(r.error, /modulation/);
    }
  });

  it("includes validated modulation when present", () => {
    const mod = {
      macros: [{ name: "Move", routes: [{ target: "filter.cutoff_hz", amount: 0.5 }] }],
    };
    const r = assembleGeneratePayload(validLlmPatchJson({ modulation: mod }), "brief");
    assert.equal(r.ok, true);
    if (r.ok) assert.deepEqual(r.payload["modulation"], mod);
  });
});

describe("handler fuzz — client input is 4xx, never 500", () => {
  const noGemini: {
    gateRateLimit: () => Promise<null>;
    getApiKey: () => string;
    enhanceBrief: () => Promise<string>;
    generatePatchText: () => Promise<{ ok: true; text: string }>;
  } = {
    gateRateLimit: async () => null,
    getApiKey: () => "test-key",
    enhanceBrief: async () => {
      throw new Error("Gemini must not be called for rejected input");
    },
    generatePatchText: async () => {
      throw new Error("Gemini must not be called for rejected input");
    },
  };

  it("oversized brief body → 400 before Gemini", async () => {
    const huge = `{"prompt":"${"x".repeat(MAX_REQUEST_BODY_BYTES)}}`;
    const res = await handleBrief(
      new Request("http://local/api/brief", { method: "POST", body: huge }),
      {
        gateRateLimit: noGemini.gateRateLimit,
        getApiKey: noGemini.getApiKey,
        enhanceBrief: noGemini.enhanceBrief,
      },
    );
    assert.equal(res.status, 400);
    const body = JSON.parse(await res.text()) as { error: string };
    assert.match(body.error, /at most/);
  });

  it("empty prompt → 400 before Gemini", async () => {
    const res = await handleBrief(
      new Request("http://local/api/brief", {
        method: "POST",
        body: JSON.stringify({ prompt: "" }),
      }),
      {
        gateRateLimit: noGemini.gateRateLimit,
        getApiKey: noGemini.getApiKey,
        enhanceBrief: noGemini.enhanceBrief,
      },
    );
    assert.equal(res.status, 400);
  });

  it("bad generate patch_id → 400 before Gemini", async () => {
    const res = await handleGenerate(
      new Request("http://local/api/generate", {
        method: "POST",
        body: JSON.stringify({ prompt: "ok", patch_id: -3 }),
      }),
      {
        gateRateLimit: noGemini.gateRateLimit,
        getApiKey: noGemini.getApiKey,
        generatePatchText: noGemini.generatePatchText,
      },
    );
    assert.equal(res.status, 400);
  });

  it("model out-of-spec patch → 502 not 500", async () => {
    const res = await handleGenerate(
      new Request("http://local/api/generate", {
        method: "POST",
        body: JSON.stringify({ prompt: "warm pad" }),
      }),
      {
        gateRateLimit: async () => null,
        getApiKey: () => "test-key",
        generatePatchText: async () => ({
          ok: true as const,
          text: validLlmPatchJson({ version: 999 }),
        }),
      },
    );
    assert.equal(res.status, 502);
    const body = JSON.parse(await res.text()) as { error: string };
    assert.ok(typeof body.error === "string" && body.error.length > 0);
  });

  it("hostile fuzz payloads → never 500", async () => {
    const briefRejects: unknown[] = [
      null,
      [],
      "",
      "not-json",
      { prompt: null },
      { prompt: 1 },
      { prompt: "" },
      { prompt: "   " },
    ];
    for (const payload of briefRejects) {
      const body =
        typeof payload === "string" ? payload : JSON.stringify(payload);
      const res = await handleBrief(
        new Request("http://local/api/brief", { method: "POST", body }),
        {
          gateRateLimit: noGemini.gateRateLimit,
          getApiKey: noGemini.getApiKey,
          enhanceBrief: noGemini.enhanceBrief,
        },
      );
      assert.notEqual(res.status, 500, `brief ${JSON.stringify(payload)}`);
      assert.ok(res.status >= 400 && res.status < 500);
    }

    const generateRejects: unknown[] = [
      ...briefRejects,
      { prompt: "ok", patch_id: 1.5 },
      { prompt: "ok", patch_id: "1" },
      { prompt: "ok", patch_id: -1 },
    ];
    for (const payload of generateRejects) {
      const body =
        typeof payload === "string" ? payload : JSON.stringify(payload);
      const res = await handleGenerate(
        new Request("http://local/api/generate", { method: "POST", body }),
        {
          gateRateLimit: noGemini.gateRateLimit,
          getApiKey: noGemini.getApiKey,
          generatePatchText: noGemini.generatePatchText,
        },
      );
      assert.notEqual(res.status, 500, `generate ${JSON.stringify(payload)}`);
      assert.ok(res.status >= 400 && res.status < 500);
    }
  });
});
