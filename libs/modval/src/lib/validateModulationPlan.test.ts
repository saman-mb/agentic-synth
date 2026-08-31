import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { validateModulationPlan } from './validateModulationPlan.ts';

describe('validateModulationPlan', () => {
  it('accepts a valid macro route', () => {
    const verdict = validateModulationPlan({
      macros: [{ name: 'Move', routes: [{ target: 'filter.cutoff_hz', amount: 0.5 }] }],
    });
    assert.equal(verdict.ok, true);
    if (verdict.ok) assert.ok(verdict.plan);
  });

  it('rejects amount 1.5', () => {
    const verdict = validateModulationPlan({
      macros: [{ routes: [{ target: 'filter.cutoff_hz', amount: 1.5 }] }],
    });
    assert.equal(verdict.ok, false);
  });

  it('rejects an unknown target', () => {
    const verdict = validateModulationPlan({
      macros: [{ routes: [{ target: 'not.a.param', amount: 0.2 }] }],
    });
    assert.equal(verdict.ok, false);
  });

  it('normalises osc[0].volume then accepts', () => {
    const verdict = validateModulationPlan({
      macros: [{ routes: [{ target: 'osc[0].volume', amount: 0.3 }] }],
    });
    assert.equal(verdict.ok, true);
  });

  it('rejects a bad extra source', () => {
    const verdict = validateModulationPlan({
      extras: [{ source: 'not-a-source', target: 'filter.cutoff_hz', amount: 0.1 }],
    });
    assert.equal(verdict.ok, false);
  });

  it('maps undefined to plan: undefined', () => {
    const verdict = validateModulationPlan(undefined);
    assert.deepEqual(verdict, { ok: true, plan: undefined });
  });
});
