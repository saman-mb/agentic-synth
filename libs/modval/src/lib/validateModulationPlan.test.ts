import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { validateModulationPlan } from './validateModulationPlan.ts';

describe('validateModulationPlan', () => {
  it('accepts a valid macro route and round-trips unchanged', () => {
    const plan = {
      macros: [{ name: 'Move', routes: [{ target: 'filter.cutoff_hz', amount: 0.5 }] }],
    };
    const verdict = validateModulationPlan(plan);
    assert.equal(verdict.ok, true);
    if (verdict.ok) assert.deepEqual(verdict.plan, plan);
  });

  it('rejects amount 1.5', () => {
    const verdict = validateModulationPlan({
      macros: [{ routes: [{ target: 'filter.cutoff_hz', amount: 1.5 }] }],
    });
    assert.equal(verdict.ok, false);
    if (!verdict.ok) {
      assert.equal(verdict.errors.length, 1);
      assert.equal(verdict.errors[0].code, 'out_of_range');
    }
  });

  it('rejects an unknown target', () => {
    const verdict = validateModulationPlan({
      macros: [{ routes: [{ target: 'not.a.param', amount: 0.2 }] }],
    });
    assert.equal(verdict.ok, false);
    if (!verdict.ok) {
      assert.equal(verdict.errors[0].code, 'unknown_target');
    }
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
    if (!verdict.ok) {
      assert.equal(verdict.errors[0].code, 'unknown_source');
    }
  });

  it('maps undefined to plan: undefined', () => {
    const verdict = validateModulationPlan(undefined);
    assert.deepEqual(verdict, { ok: true, plan: undefined });
  });

  it('enumerates multiple violations in one plan', () => {
    const verdict = validateModulationPlan({
      macros: [{
        routes: [
          { target: 'not.a.param', amount: 2 },
          { target: 'filter.cutoff_hz', amount: 0.5 },
        ],
      }],
      extras: [{ source: 'bogus', target: 'also.bad', amount: 3 }],
    });
    assert.equal(verdict.ok, false);
    if (!verdict.ok) {
      assert.ok(verdict.errors.length >= 3);
      const codes = new Set(verdict.errors.map((e) => e.code));
      assert.ok(codes.has('unknown_target'));
      assert.ok(codes.has('out_of_range'));
      assert.ok(codes.has('unknown_source'));
      assert.match(verdict.error, /;/);
    }
  });

  it('allows multiple routes to the same destination (not a cycle under current graph)', () => {
    // Cyclic routing is a documented non-goal: destinations are PARAM_RANGES
    // keys, never modulation sources, so source→param→source edges do not exist.
    const plan = {
      macros: [{
        routes: [
          { target: 'filter.cutoff_hz', amount: 0.2 },
          { target: 'filter.cutoff_hz', amount: -0.1 },
        ],
      }],
      extras: [{ source: 'lfo1', target: 'filter.cutoff_hz', amount: 0.3 }],
    };
    const verdict = validateModulationPlan(plan);
    assert.equal(verdict.ok, true);
    if (verdict.ok) assert.deepEqual(verdict.plan, plan);
  });
});
