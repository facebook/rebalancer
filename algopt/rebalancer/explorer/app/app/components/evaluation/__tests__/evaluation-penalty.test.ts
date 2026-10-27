import {describe, expect, it} from 'vitest';

import {
  hasAnyPenalty,
  isRowChanged,
  withAllPenaltiesShown,
  withTablePenaltyShown,
} from '@/app/components/evaluation/evaluation-penalty';
import type {
  ExpressionResult,
  ExpressionType,
} from '@/lib/rebalancer-explorer-types';

function expression(penaltyValue?: number | null): ExpressionResult {
  return {
    id: 1,
    type: 0 as ExpressionType,
    name: 'objective',
    description: '',
    value: 0,
    tupleIndex: 0,
    penaltyValue,
  };
}

describe('hasAnyPenalty', () => {
  it('is false when no expression has a penalty', () => {
    expect(hasAnyPenalty([expression(), expression(null)])).toBe(false);
  });

  it('is true when one expression has a penalty, even a zero one', () => {
    expect(hasAnyPenalty([expression(), expression(0)])).toBe(true);
  });
});

describe('isRowChanged', () => {
  it('keeps a row whose value changed', () => {
    expect(isRowChanged({delta: 1, penaltyDelta: 0}, false)).toBe(true);
  });

  it('keeps a penalty-only change only while penalties are shown', () => {
    const penaltyOnly = {delta: 0, penaltyDelta: -2};
    expect(isRowChanged(penaltyOnly, true)).toBe(true);
    expect(isRowChanged(penaltyOnly, false)).toBe(false);
  });

  it('drops a row where neither value nor penalty changed', () => {
    expect(isRowChanged({delta: 0, penaltyDelta: 0}, true)).toBe(false);
  });
});

describe('penalty visibility', () => {
  it('hides and shows one table', () => {
    const hidden = withTablePenaltyShown(new Set(), 2, false);
    expect([...hidden]).toEqual([2]);
    expect([...withTablePenaltyShown(hidden, 2, true)]).toEqual([]);
  });

  it('hides every table and shows them all again', () => {
    expect([...withAllPenaltiesShown(false, 3)]).toEqual([0, 1, 2]);
    expect([...withAllPenaltiesShown(true, 3)]).toEqual([]);
  });
});
