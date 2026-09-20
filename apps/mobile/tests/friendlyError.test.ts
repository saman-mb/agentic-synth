import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { friendlyError } from '../src/services/friendlyError.ts';

describe('friendlyError', () => {
  it('returns calm 429 copy', () => {
    assert.match(friendlyError(429), /Rate limited/i);
  });

  it('prefers server detail on 429', () => {
    assert.equal(friendlyError(429, 'Slow down — retry in 30s'), 'Slow down — retry in 30s');
  });

  it('handles empty prompt 400', () => {
    assert.match(friendlyError(400, 'prompt must be non-empty'), /rejected/i);
  });
});
