---
sidebar_position: 1
---

# Core Concepts

To ensure reusability of the specs, Rebalancer defines a set of modeling constructs that can flexibly represent user requirements.

## Assignment Problem

Any assignment problem that aims to place objects into a pre-defined set of containers can be modeled as a Rebalancer Assignment Problem.

![Objects assigned to bins while minimizing objectives and satisfying constraints](pathname:///img/core-concepts/assignment-problem.png)

A Rebalancer problem can be built using the following modeling constructs:

* **Object**: Define what objects are in the problem
* **Container**: Define what containers are in the problem
* **Dimension**: Dimensions can be used to model *properties* of objects and containers.
   * For example, if objects are tasks that we want to place in servers (containers), the CPU request of a task could be modeled as a task dimension and the CPU capacity of a server can be modeled as a container dimension.

* **Scopes and Scope Items**: Scopes can be used to group containers based on common properties.
   * In a datacenter of servers, servers belong to distinct racks. A scope can be used to specify the partitioning of servers into distinct racks. This is useful when we want to define some objectives or constraints at a higher level of logical grouping rather than on individual containers.

* **Partition and Groups**: Partitions can be used to group objects based on common properties.
   * For example, in a task assignment problem where we want to assign tasks to servers, multiple tasks may belong to the same job. A partition can be used to encode this information on what distinct jobs exist in the problem and which tasks belong to which job. Similar to scopes, partitions can be used to define objectives and constraints at a higher level of grouping rather than on individual objects.

## Dimensions

A dimension is a mapping of each object and container to a number that captures some real world property of interest. For example, the memory dimension of a server (a container) specifies the server's memory capacity, while the memory dimension of a task (an object) specifies the amount of memory needed to run the task.

* Dimensions can also represent complex relationships. For example, we can define a `prohibitedObjects` dimension, where an object takes a value of 1 or 0, depending on its assignability to a bin.

![Tasks with CPU load and a machine with CPU capacity, modeled as dimensions](pathname:///img/core-concepts/dimensions.png)

### Dynamic Dimensions

The examples of object dimensions seen so far are **static** — such as the memory dimension of a task — and only depend on the object. Rebalancer also supports the notion of dynamic dimensions where the contribution of an object also depends on the container it is assigned to. For example, depending on the (container) server it is assigned to, the memory/CPU requirements of a task can vary.

Another place where dynamic dimensions are used is to [avoid certain assignments](../reference/placement/avoid-assignments).

## Scopes

Rebalancer uses scopes to represent the hierarchical structure of containers. For example, the `datacenter` and `rack` scopes represent servers in a datacenter or rack. *A scope divides containers into sets called scope items*.

For example, under the `rack` scope, the scope items `rack1` and `rack2` represent the set of servers in those specific racks.

![A scope dividing containers into scope items, such as racks](pathname:///img/core-concepts/scopes.png)

## Partitions

Similar to scopes and scope items for containers, an object partition is an aggregation of objects, which may not be necessarily disjoint. *Each set in the partition is referred to as a group*.

For example, in the context of cluster management, all tasks are partitioned into jobs and a job is a group of tasks that run the same executable.

![An object partition grouping tasks into jobs](pathname:///img/core-concepts/partitions.png)

## Goals and Constraints

Once a problem is described in terms of objects, containers, dimensions, scopes, and partitions, you express *what makes a good assignment* using **specs**. A spec is a reusable, named building block that captures a common modeling need — such as keeping utilization under a limit (**capacity**), spreading load evenly (**balance**), keeping a group's members apart (**diversity**), or reducing churn (**minimize movement**). Each spec refers back to the constructs you have already defined: a `CapacitySpec`, for example, names a `dimension` to measure and a `scope` over which the limit applies, while a group-based spec such as `GroupCountSpec` additionally names a `partition`.

A spec is applied to a problem in one of two ways:

* **Constraint**: a hard requirement that the solution must satisfy. If the initial assignment already violates it, Rebalancer treats it as a high-priority objective and tries hard to repair it.
* **Goal**: a soft objective that Rebalancer minimizes as well as possible, traded off against other goals according to its weight (or a strict priority order).

Most specs can be used as **either** a constraint or a goal — the same `CapacitySpec` can require that memory is never exceeded (constraint) or merely encourage it (goal). A few are inherently one or the other: `MinimizeMovementSpec` only makes sense as a goal, while `AvoidAssignmentsSpec` only makes sense as a constraint.

See the [Goals & Constraints Reference](../reference/) for the full catalog of available specs and whether each can act as a goal, a constraint, or both.

## Example

![Figure 1: Rebalancer specs for assigning tasks to servers](pathname:///img/core-concepts/example.png)

As a concrete example of using these constructs, **Figure 1** defines two dimensions, CPU and storage, to model resources; a rack scope as a fault domain; and a job partition where each group comprises tasks that run the same executable.
