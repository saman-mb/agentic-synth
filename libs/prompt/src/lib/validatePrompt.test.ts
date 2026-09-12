import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_PROMPT_LENGTH, validatePrompt } from './validatePrompt.ts';
import { parseBriefResponse, briefRequestBody } from './briefTypes.ts';

describe('validatePrompt', () => {
  it('accepts a non-empty in-range prompt', () => {
    const verdict = validatePrompt('warm pad');
    assert.deepEqual(verdict, { ok: true, prompt: 'warm pad' });
  });

  it('rejects empty / whitespace prompts', () => {
    for (const bad of ['', '   ', '\n\t']) {
      const verdict = validatePrompt(bad);
      assert.equal(verdict.ok, false);
      if (!verdict.ok) {
        assert.equal(verdict.error, 'prompt must be a non-empty string.');
      }
    }
  });

  it('rejects a non-string prompt', () => {
    const verdict = validatePrompt(42);
    assert.equal(verdict.ok, false);
    if (!verdict.ok) {
      assert.equal(verdict.error, 'prompt must be a non-empty string.');
    }
  });

  it('rejects an over-length prompt', () => {
    const long = 'x'.repeat(MAX_PROMPT_LENGTH + 1);
    const verdict = validatePrompt(long);
    assert.equal(verdict.ok, false);
    if (!verdict.ok) {
      assert.equal(
        verdict.error,
        `prompt must be at most ${MAX_PROMPT_LENGTH} characters.`,
      );
    }
  });

  it('accepts a prompt at the exact length cap', () => {
    const exact = 'y'.repeat(MAX_PROMPT_LENGTH);
    assert.deepEqual(validatePrompt(exact), { ok: true, prompt: exact });
  });
});

describe('brief types', () => {
  it('builds a brief request body', () => {
    assert.deepEqual(briefRequestBody('hi'), { prompt: 'hi' });
  });

  it('parses a happy-path brief response', () => {
    assert.deepEqual(parseBriefResponse({ brief: 'SONIC CHARACTER: soft' }), {
      ok: true,
      brief: 'SONIC CHARACTER: soft',
    });
  });

  it('rejects malformed brief JSON', () => {
    assert.equal(parseBriefResponse(null).ok, false);
    assert.equal(parseBriefResponse({}).ok, false);
    assert.equal(parseBriefResponse({ brief: 1 }).ok, false);
    const err = parseBriefResponse({ error: 'prompt must be a non-empty string.' });
    assert.deepEqual(err, {
      ok: false,
      error: 'prompt must be a non-empty string.',
    });
  });
});
