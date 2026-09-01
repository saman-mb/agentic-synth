import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_PROMPT_LENGTH } from './validatePrompt.ts';
import { MAX_REQUEST_BODY_BYTES } from './requestLimits.ts';
import { validateBriefRequest } from './validateBriefRequest.ts';
import { validateGenerateRequest } from './validateGenerateRequest.ts';

describe('MAX_REQUEST_BODY_BYTES', () => {
  it('is large enough for prompt+brief but finite', () => {
    assert.ok(MAX_REQUEST_BODY_BYTES >= MAX_PROMPT_LENGTH + 4000);
    assert.ok(MAX_REQUEST_BODY_BYTES <= 64 * 1024);
  });
});

describe('validateBriefRequest', () => {
  it('accepts a valid brief body', () => {
    assert.deepEqual(validateBriefRequest({ prompt: 'warm pad' }), {
      ok: true,
      prompt: 'warm pad',
    });
  });

  it('rejects non-object bodies', () => {
    for (const bad of [null, [], 'x', 1]) {
      const v = validateBriefRequest(bad);
      assert.equal(v.ok, false);
      if (!v.ok) {
        assert.equal(v.error, 'Request body must be a JSON object.');
      }
    }
  });

  it('rejects empty prompt via shared validatePrompt', () => {
    const v = validateBriefRequest({ prompt: '  ' });
    assert.equal(v.ok, false);
    if (!v.ok) {
      assert.equal(v.error, 'prompt must be a non-empty string.');
    }
  });
});

describe('validateGenerateRequest', () => {
  it('accepts prompt-only with default patch_id', () => {
    assert.deepEqual(validateGenerateRequest({ prompt: 'bass' }), {
      ok: true,
      prompt: 'bass',
      patchId: 0,
      brief: undefined,
    });
  });

  it('accepts patch_id and brief', () => {
    assert.deepEqual(
      validateGenerateRequest({
        prompt: 'bass',
        patch_id: 3,
        brief: 'SONIC CHARACTER: deep',
      }),
      {
        ok: true,
        prompt: 'bass',
        patchId: 3,
        brief: 'SONIC CHARACTER: deep',
      },
    );
  });

  it('rejects negative patch_id', () => {
    const v = validateGenerateRequest({ prompt: 'bass', patch_id: -1 });
    assert.equal(v.ok, false);
    if (!v.ok) {
      assert.equal(v.error, 'patch_id must be an integer >= 0.');
    }
  });

  it('ignores non-string brief', () => {
    const v = validateGenerateRequest({ prompt: 'bass', brief: 42 });
    assert.equal(v.ok, true);
    if (v.ok) assert.equal(v.brief, undefined);
  });
});
