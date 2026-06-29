---
sidebar_position: 3
---

# Utilization Definitions

In [CapacitySpec](../specs/capacity) and several other specs (such as BalanceSpec,
and GroupCountSpec), the `definition` parameter controls **which
objects count toward a scope item's utilization**. This page explains every
choice with one running example: an object `x` moving between two scope items,
the **source** (which it leaves) and the **destination** (which it enters).

## The running example

Throughout, we track this single move and ask, for each definition, which objects
count toward the **source**'s utilization and which toward the **destination**'s.

```
legend:  a = stays in source     x = moves source -> destination     b = stays in destination

            initial                       final
 source:  [ a   x ]        ----->        [ a     ]
 dest:    [ b     ]        ----->        [ b   x ]
```

Each definition counts a combination of three sets of objects for a scope item:
those in it **initially** (before the move), those in the **final** assignment
(after the move), and those that **stayed** (in it both before and after). For the
source these are initial `{a, x}`, final `{a}`, stayed `{a}`; for the destination,
initial `{b}`, final `{b, x}`, stayed `{b}`. The definitions are:

| Definition | Counts (using the three sets) |
|------------|-------------------------------|
| `AFTER` | final |
| `DURING` | final + initial - stayed |
| `NEW` | final - stayed |
| `OLD` | initial - stayed |
| `MOVED_DATA` | (final - stayed) + (initial - stayed) |
| `DURING_AND_AFTER` | an `AFTER` spec plus a `DURING` spec |
| `DOUBLE_DURING` | `DURING` + `OLD` |
| `DOUBLE_DURING_AND_AFTER` | `DOUBLE_DURING` + `AFTER` |

In the sections below, the objects that **count** toward a scope item's
utilization are listed in `{ }` (an object listed twice is counted twice).

## AFTER

The most natural definition, and the default: the objects placed in a scope item
*after* the proposed moves are performed, i.e. the objects in the scope item in
the final assignment.

```
 source counts:  { a }        (x has moved out, so it no longer counts)
 dest   counts:  { b, x }     (x has moved in)
```

Use `AFTER` for a standard capacity limit: "the final placement must fit".

## DURING

Counts an object in **both** the scope item it starts in and the scope item it
moves to. This is useful when a move is not instantaneous in the real system: for
example, a replica may have to be loaded on the destination before being dropped
from the source, so for a while it consumes resources on both.

```
 source counts:  { a, x }     (x still counts here while the move is in flight)
 dest   counts:  { b, x }     (x already counts here too)
```

`x` is double-counted across the two scope items.

### Why you should not use DURING alone

Prefer [`DURING_AND_AFTER`](#during_and_after) instead:

- **`DURING` is a poor goal.** The initial assignment is already optimal for it:
  moving objects can only *increase* the `DURING` utilization of a scope item, so
  there is nothing to minimize.
- **`DURING` alone is a poor constraint.** Adding a `DURING` constraint to keep
  in-flight moves within capacity is reasonable, but if the constraint is
  initially broken Rebalancer converts it into a goal, and (per the point above)
  `DURING` is a poor goal, so Rebalancer has no incentive to fix it. Pairing it
  with `AFTER` gives Rebalancer something to minimize.

## NEW

Counts only objects newly moved **into** the scope item. Useful when you want to
limit incoming work: for example, loading a replica onto a host consumes network
bandwidth, so you may want to cap how much can arrive at once.

```
 source counts:  { }          (nothing moved in)
 dest   counts:  { x }
```

## OLD

Counts only objects that started in the scope item but have moved **out** of it
(they are placed elsewhere in the final assignment).

```
 source counts:  { x }        (x left, so it counts as "old" here)
 dest   counts:  { }
```

## MOVED_DATA

Counts objects that moved **in or out** of the scope item, equivalent to `NEW`
plus `OLD`. Useful to limit the total churn (data moved) at a scope item.

```
 source counts:  { x }        (x moved out)
 dest   counts:  { x }        (x moved in)
```

## DURING_AND_AFTER

Shorthand for defining two specs at once with otherwise identical fields:

- one with definition `AFTER`, and
- one with definition `DURING`.

**As a constraint** this is the common, recommended way to bound in-flight
capacity. As a hard constraint `DURING` is stricter than `AFTER`, and when the
constraint is initially broken the `AFTER` component is what gives Rebalancer the
incentive to fix it (see [Why you should not use DURING
alone](#why-you-should-not-use-during-alone)).

**As a goal** there is no reason to use `DURING_AND_AFTER`: the `DURING`
component cannot be minimized below its initial value, so you almost certainly
mean to minimize `AFTER` on its own.

## DOUBLE_DURING

Equivalent to applying the capacity spec twice: once with `DURING` and once with
`OLD`.

```
 source counts:  { a, x, x }  (DURING: a, x  +  OLD: x)
 dest   counts:  { b, x }     (DURING: b, x  +  OLD: nothing)
```

## DOUBLE_DURING_AND_AFTER

Equivalent to applying the capacity spec with `DOUBLE_DURING` and again with
`AFTER`. Equivalently, it is the combination of `DURING`, `OLD`, and `AFTER`.

## Choosing a definition

| Scenario | Definition |
|----------|-----------|
| Standard capacity limit on the final placement | `AFTER` |
| Keep capacity safe while moves are in flight | `DURING_AND_AFTER` |
| Limit incoming work per scope item | `NEW` |
| Limit outgoing work per scope item | `OLD` |
| Limit total churn per scope item | `MOVED_DATA` |
