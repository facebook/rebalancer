---
sidebar_position: 2
---

# Tutorial: Build Your First Model

## Overview

Rebalancer is a library for optimizing **assignment problems**, where the goal is to find an optimal assignment of objects to containers subject to different constraints and objectives.

Examples of assignment problems are:

* Placing server racks into datacenters
* Distributing jobs across machines
* Routing user traffic to web clusters
* Classic puzzles such as [Sudoku](https://github.com/facebookincubator/rebalancer/tree/main/algopt/rebalancer/examples/sudoku) and the [eight queens problem](https://github.com/facebookincubator/rebalancer/tree/main/algopt/rebalancer/examples/eightqueens)

In this tutorial we'll build a solver for a small but realistic problem: placing **tasks onto hosts** in a compute cluster.

You don't need any prior context about Rebalancer to get started. We'll introduce the key concepts as we build the example.

## What We're Going to Build

Our example has **12 tasks** that start out crammed onto a single host, plus **4 hosts** to spread them across. We'll build a model that places the tasks while considering the following features:

* **Balance memory:** even out memory utilization across the hosts.
* **Drain a host:** keep `host0` empty, because it's about to go down for maintenance.
* **Spread for safety:** keep the two tasks of each job in different racks, so one rack failure can't take a whole job down.

While a production model would have more moving parts (multiple resources like CPU and disk, or multiple failure domains like region and datacenter), the ideas are the same.

All the code below comes from a single runnable file, linked in the [Full example](#full-example) section.

## Defining the Problem

First we create a `ProblemSolver`. It holds the model, which we build up with the methods below, and then computes a solution. We use `ProblemSolverFactory`, which sets up a default thread pool for us; we just give it a service name and scope (used for logging).

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=solver_instance_start end=solver_instance_end
```

With the solver created, every model needs three basic things:

* **Object name:** a short name for the things we place. Here an object is a *task*.
* **Container name:** a short name for where they go. Here a container is a *host*.
* **Initial assignment:** which objects start in which containers. This serves two purposes: it tells Rebalancer the full set of tasks and hosts to work with, and it records where everything starts, which some objectives compare against---for example, "move as few tasks as possible".

In our example we have 12 tasks and 4 hosts, and all tasks start on `host0`.

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=basic_attrs_start end=basic_attrs_end
```

The names are just strings; you can name tasks and hosts however you like. The `task0`, `host0`, ... pattern is only a convention we picked to keep the example easy to follow.

## Dimensions

To balance memory, we first have to tell Rebalancer how much memory things use. A **dimension** is a named value attached to objects and containers that represents some real-world attribute like memory, CPU, or disk---in this case, `memory`.

First, how much memory each task needs. To keep things simple, we will encode that every task needs 1 GB:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=obj_dim_start end=obj_dim_end
```

Next, how much memory each host has. Each host has 10 GB, except `host1` which has 20 GB:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=container_dim_start end=container_dim_end
```

You can define as many dimensions as you need (CPU, disk, and so on) and use them in the goals and constraints that follow.

## Goals

A **goal** (also called an *objective*) describes something to *optimize*: the closer the solution gets to it, the better. A model can have several goals.

Let's add a goal that balances memory use across hosts. For this, we use a `BalanceSpec` goal on the `memory` dimension we just defined:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=balance_start end=balance_end
```

A `BalanceSpec` goal pushes every host toward the same *relative utilization*---memory used divided by memory available. Here we use the default `LINEAR` formula; you can read more about `BalanceSpec` and the formulas it supports in the `BalanceSpec` reference.

`addGoal()` also takes two optional arguments. The *weight* (second argument, default 1) sets a goal's influence when several goals are combined into a weighted sum. The *tuple position* (third argument) puts a goal in its own priority tier: the solver fully optimizes a higher tier before trading anything away for a lower one. We use the defaults here.

## Scopes

Real-world hosts are usually grouped---for example into racks, where one rack holds several hosts. A **scope** groups *containers* together so you can write goals and constraints at that level.

Here we list the hosts that belong to each rack: `rack0` holds `host0` and `host1`, and `rack1` holds the other two:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=scope_start end=scope_end
```

A container is really the smallest possible scope---that's why we could use `host` directly as the scope of the `BalanceSpec` above.

## Partitions

Just as scopes group containers, **partitions** group *objects*. (Easy way to remember it: scopes are for containers, partitions are for objects.)

Let's group tasks into jobs of two tasks each---`task0` and `task1` form `job0`, `task2` and `task3` form `job1`, and so on. We'll use these jobs in the fault-tolerance constraint in the next section:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=partition_start end=partition_end
```

## Constraints

A **constraint** is a hard requirement that a valid solution must *satisfy*. Unlike a goal, which is best-effort, a constraint must always hold.

First, `host0` is going down for maintenance, so we need it empty. A `ToFreeSpec` constraint says `host0` must hold no tasks in a valid solution:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=tofree_start end=tofree_end
```

Second, we want each job to survive a rack failure: if a rack loses power, every host in it goes offline at once, so we keep a job's two tasks in different racks. Using the `job` partition and the `rack` scope, a group count constraint with a limit of 1 says: at most one task of any job per rack.

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=groupcount_start end=groupcount_end
```

## Solving the Problem

The model is complete. Now we choose a **solver** to compute the placement. Rebalancer offers a few strategies; for this example we use the [local search](../solvers/local-search) solver, a good default that scales to large problems. We run it and print where each task ended up:

```cpp file=algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp start=solve_print_start end=solve_print_end
```

While solving, Rebalancer writes progress logs to standard error, so you can watch what it's doing. The final result here is printed below a header and looks like this (the exact placement may vary between runs):

```
=== Final placement ===
host1 (rack0): task1(job0) task10(job5) task2(job1) task4(job2) task6(job3) task9(job4)
host2 (rack1): task11(job5) task3(job1) task5(job2)
host3 (rack1): task0(job0) task7(job3) task8(job4)
```

Each line shows a host, its rack, and the job each task belongs to. This solution respects every constraint and reflects the objectives we set:

* `host0` is empty, so it doesn't appear.
* `host1` (rack0) holds twice as many tasks as the other hosts, so every host sits at about 30% memory utilization.
* Each job's two tasks land in different racks, so no single rack failure can take down a whole job.

In general a perfect solution isn't always possible---one that perfectly balances every task, for instance. Rebalancer returns the best solution it can find given the set of objectives and constraints.

## Build and Run

The default build produces only the Rebalancer library; the examples are gated behind a CMake option. Assuming you've already built Rebalancer from source (see [Installation](installation)), re-run CMake with `-DEXAMPLES=ON`, then build and run this example's target:

```bash
# From the rebalancer/build/ directory
cmake -GNinja -DEXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug ..
ninja tasks_on_hosts.exe
./tasks_on_hosts.exe
```

You should see the `=== Final placement ===` output shown above.

## Full Example

The complete, runnable source for this tutorial is [`tasks_on_hosts.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/examples/website/getting_started/tasks_on_hosts/tasks_on_hosts.cpp).

## Wrapping Up

Congratulations! You've built and solved your first model with Rebalancer. You've seen the core building blocks---objects and containers, dimensions, goals, constraints, scopes, and partitions---and you should be ready to start modeling your own problems.

Here are a few references as you explore more of Rebalancer:
* [Goals & Constraints](../reference/): a list of all the specs available and their documentation.
* [Solvers](../solvers/overview): learn about the different solving strategies available and their trade-offs.
* [Core Concepts](../core-concepts/overview): a deeper look at objects, containers, dimensions, scopes, and partitions.
* [GitHub repository](https://github.com/facebookincubator/rebalancer): source code, issues, and discussions.
