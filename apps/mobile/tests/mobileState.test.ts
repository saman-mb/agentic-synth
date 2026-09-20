import { registerHooks } from 'node:module';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const aliases: Record<string, string> = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
  '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src/index.ts'),
};

registerHooks({
  resolve(specifier, context, nextResolve) {
    const mapped = aliases[specifier];
    if (mapped) return nextResolve(pathToFileURL(mapped).href, context);
    if (!specifier.startsWith('node:') && !/\.[cm]?[jt]sx?$/.test(specifier) && (specifier.startsWith('.') || specifier.startsWith('/'))) {
      try {
        return nextResolve(`${specifier}.ts`, context);
      } catch {
        // fall through
      }
    }
    return nextResolve(specifier, context);
  },
});

const { INITIAL_SESSION } = await import('../src/state/mobileState.ts');
const {
  addUserMessage,
  addAgentMessage,
  addSystemMessage,
  setGenerating,
  updateActiveMacros,
  activatePatchCard,
} = await import('../src/state/mobileStateMachine.ts');

describe('MobileState Conversational Model', () => {
  it('starts with a welcome message in INITIAL_SESSION', () => {
    assert.equal(INITIAL_SESSION.messages.length, 1);
    assert.equal(INITIAL_SESSION.messages[0].role, 'agent');
    assert.equal(INITIAL_SESSION.viewMode, 'chat');
  });

  it('adds user message and toggles generating state', () => {
    let session = addUserMessage(INITIAL_SESSION, 'Deep warm reese bass');
    assert.equal(session.messages.length, 2);
    assert.equal(session.messages[1].role, 'user');
    assert.equal(session.messages[1].text, 'Deep warm reese bass');

    session = setGenerating(session, true);
    assert.equal(session.isGenerating, true);
  });

  it('adds agent message with PatchCard and activates it', () => {
    const patchCard = {
      patch: {} as any,
      macros: [0.5, 0.5, 0.5, 0.5],
      prompt: 'Deep warm reese bass',
      brief: 'Reese bass',
    };
    const session = addAgentMessage(INITIAL_SESSION, "Here's a deep warm reese bass", patchCard);
    assert.equal(session.messages.length, 2);
    const agentMsg = session.messages[1];
    assert.equal(agentMsg.role, 'agent');
    assert.equal(session.activePatchCardId, agentMsg.id);
    assert.equal(session.isGenerating, false);

    // Update macros
    const updated = updateActiveMacros(session, [0.8, 0.4, 0.2, 0.9]);
    const card = updated.messages.find((m) => m.id === updated.activePatchCardId)?.patchCard;
    assert.deepEqual(card?.macros, [0.8, 0.4, 0.2, 0.9]);
  });

  it('adds system messages for status or errors', () => {
    const session = addSystemMessage(INITIAL_SESSION, 'Saved to library');
    const lastMsg = session.messages[session.messages.length - 1];
    assert.equal(lastMsg.role, 'system');
    assert.equal(lastMsg.text, 'Saved to library');
  });

  it('switches active patch card', () => {
    const card1 = { patch: {} as any, macros: [0.1, 0.2, 0.3, 0.4], prompt: 'Sound 1', brief: 'b1' };
    const card2 = { patch: {} as any, macros: [0.5, 0.6, 0.7, 0.8], prompt: 'Sound 2', brief: 'b2' };

    let session = addAgentMessage(INITIAL_SESSION, 'Card 1', card1);
    const id1 = session.activePatchCardId!;
    session = addAgentMessage(session, 'Card 2', card2);
    const id2 = session.activePatchCardId!;
    assert.equal(session.activePatchCardId, id2);

    session = activatePatchCard(session, id1);
    assert.equal(session.activePatchCardId, id1);
  });
});
