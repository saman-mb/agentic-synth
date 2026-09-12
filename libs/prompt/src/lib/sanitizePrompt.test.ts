import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { sanitizePrompt } from './sanitizePrompt.ts';

describe('sanitizePrompt', () => {
  it('maps horror → uneasy', () => {
    assert.equal(sanitizePrompt('horror'), 'uneasy');
  });

  it('maps Horror → Uneasy', () => {
    assert.equal(sanitizePrompt('Horror'), 'Uneasy');
  });

  it('maps HORROR → Uneasy (first char only)', () => {
    assert.equal(sanitizePrompt('HORROR'), 'Uneasy');
  });

  it('leaves killer unchanged', () => {
    assert.equal(sanitizePrompt('killer'), 'killer');
  });

  it('maps kill_joy → drop_joy (isalnum, not word-boundary)', () => {
    assert.equal(sanitizePrompt('kill_joy'), 'drop_joy');
  });

  it('is idempotent on already-sanitized text', () => {
    const once = sanitizePrompt('a horror pad');
    assert.equal(once, 'a uneasy pad');
    assert.equal(sanitizePrompt(once), once);
  });
});
