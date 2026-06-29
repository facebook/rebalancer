---
sidebar_position: 1
---

# Limit

A `Limit` is the bound a spec enforces on a value (for example, a scope item's
utilization in [CapacitySpec](../specs/capacity)). It is a shared building block used
by many specs.

Different specs honor different subsets of its fields. **Setting a field a spec
does not support throws an error**, so check the spec's own reference for the
subset it uses.

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `type` | LimitType | `RELATIVE` | Whether the limit is relative (a fraction) or absolute (a raw value). See [LimitType](#limittype). |
| `globalLimit` | double | 1 | The limit applied to every entry by default. |
| `scopeItemLimits` | `map<string, double>` | {} | Per-scope-item overrides, keyed by scope item name. |
| `groupLimits` | `map<string, double>` | {} | Per-group overrides, keyed by group name (group-aware specs only). |
| `scopeItemToGroupLimits` | `map<string, map<string, double>>` | {} | Per (scope item, group) overrides (group-aware specs only). |
| `isDefaultLimitUnbounded` | bool | false | If true, the fallback default is positive infinity (unbounded) instead of `globalLimit`. |

## Resolution order

A spec looks up the limit for a specific scope item (and, for group-aware specs,
a specific group). It uses the first of these you have set, from most specific to
least specific:

| Set this... | ...to limit |
|-------------|-------------|
| `scopeItemToGroupLimits` | a single (scope item, group) pair |
| `scopeItemLimits` | a single scope item |
| `groupLimits` | a single group |
| `globalLimit` | everything not covered above |

Specs without groups only use `scopeItemLimits`, then `globalLimit`. Setting
`isDefaultLimitUnbounded` replaces that fallback with positive infinity:
`globalLimit` is then ignored, so any entry without an override is unbounded
(per-scope-item and per-group overrides still apply).

## LimitType

| `type` | The limit is... |
|--------|-----------------|
| `RELATIVE` (default) | a fraction of a reference value the spec chooses |
| `ABSOLUTE` | a raw value |

What `RELATIVE` is measured against depends on the spec. For example, when
[CapacitySpec](../specs/capacity) limits a scope item's utilization, the reference is
that scope item's capacity (its same-named dimension): a `RELATIVE` limit of
`0.8` means "at most 80% of the capacity", while an `ABSOLUTE` limit of `0.8`
means "at most 0.8". If a spec divides by a scope item's dimension value that is
not defined, that value defaults to `1`, so `RELATIVE` then behaves like
`ABSOLUTE`.

## Applicability

Each spec documents which fields it honors. For example,
[CapacitySpec](../specs/capacity) uses `type`, `globalLimit`, and `scopeItemLimits`;
group-aware specs additionally use `groupLimits` / `scopeItemToGroupLimits`.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`Limit`)
