import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { INITIAL_SESSION } from '../src/state/mobileState.ts';
import { bootToHear, canTransition, transitionSession } from '../src/state/mobileStateMachine.ts';

describe('MobileState FSM', () => {
  it('allows idle → hear boot path for demo patch', () => {
    assert.equal(canTransition('idle', 'hear'), true);
    const next = bootToHear(INITIAL_SESSION);
    assert.equal(next.state, 'hear');
    assert.equal(next.isPlaying, true);
  });

  it('rejects illegal say → shape shortcut', () => {
    assert.equal(canTransition('say', 'shape'), false);
    assert.throws(() =>
      transitionSession({ ...INITIAL_SESSION, state: 'say' }, 'shape'),
    );
  });

  it('stores returnState on error entry', () => {
    const next = transitionSession(INITIAL_SESSION, 'error', { returnState: 'say' });
    assert.equal(next.state, 'error');
    assert.equal(next.returnState, 'say');
  });
});
