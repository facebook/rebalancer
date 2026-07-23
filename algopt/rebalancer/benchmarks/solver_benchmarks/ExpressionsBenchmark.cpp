// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include <algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h>

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/init/Init.h>
#include <folly/Random.h>

using namespace facebook::rebalancer;

namespace {

folly::Random::DefaultGenerator rng(0 /* seed */);

struct BenchmarkUniverse : public entities::tests::UniverseBuilderTestUtils {
  // Build universe placing object i in container (i % containerCount).
  std::shared_ptr<const entities::Universe> build(
      int objectCount,
      int containerCount) {
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(containerCount)) {
      initialAssignment[fmt::format("container{}", i)] = {};
    }
    for (const auto i : folly::irange(objectCount)) {
      initialAssignment[fmt::format("container{}", i % containerCount)]
          .push_back(fmt::format("object{}", i));
    }
    setInitialAssignment(initialAssignment);
    return buildUniverse();
  }
};

inline std::shared_ptr<const entities::Universe> buildUniverse(
    int objectCount,
    int containerCount) {
  return BenchmarkUniverse().build(objectCount, containerCount);
}

inline entities::ObjectId object(
    int i,
    const std::shared_ptr<const entities::Universe>& universe) {
  return universe->getObjectId(fmt::format("object{}", i));
}

inline entities::ContainerId container(
    int i,
    const std::shared_ptr<const entities::Universe>& universe) {
  return universe->getContainerId(fmt::format("container{}", i));
}

double partialApply(
    Orchestrator& orchestrator,
    Expression* expr,
    const Assignment& assignment,
    Context& context) {
  orchestrator.apply(context, assignment);
  const auto val = context.apply().get(expr->getId());
  return val.has_value() ? val.value() : expr->value;
}

double
evaluate(Orchestrator& orchestrator, Expression* expr, Context& context) {
  orchestrator.evaluate(expr, context);
  const auto val = context.val().get(expr->getId());
  return val.has_value() ? val.value() : expr->value;
}
} // namespace

static ChangeSet createLargeChangeSet(
    int objectCount,
    int containerCount,
    const std::shared_ptr<const entities::Universe>& universe) {
  // create a large set of changes; every object moves to an adjacent container
  std::vector<Change> changes;
  for (const auto i : folly::irange(objectCount)) {
    const auto initialContainer = i % containerCount;
    const auto finalContainer = (i) % containerCount;
    changes.emplace_back(
        object(i, universe), container(initialContainer, universe), -1);
    changes.emplace_back(
        object(i, universe), container(finalContainer, universe), 1);
  }
  return ChangeSet(std::move(changes));
}

static ChangeSet createSmallChangeSet(
    int objectCount,
    int containerCount,
    int nChanges,
    const std::shared_ptr<const entities::Universe>& universe) {
  // create 'nChanges' number of random changes; every object moves to an
  // adjacent container
  ChangeSet changeSet;
  for (const auto _ : folly::irange(nChanges)) {
    const auto randomObj = folly::Random::rand32(rng) % objectCount;
    const auto initialContainer = randomObj % containerCount;
    const auto finalContainer = (randomObj + 1) % containerCount;

    changeSet.insert(Change(
        object(randomObj, universe),
        container(initialContainer, universe),
        -1));
    changeSet.insert(Change(
        object(randomObj, universe), container(finalContainer, universe), 1));
  }
  return changeSet;
}

static ExprPtr createObjectVector(
    int nNonZeroObjects,
    int totalObjects,
    const std::shared_ptr<const entities::Universe>& universe) {
  PackerMap<entities::ObjectId, double> objectToValue;
  while (static_cast<int>(objectToValue.size()) < nNonZeroObjects) {
    const int id = objectToValue.size();
    objectToValue[object(id, universe)] = id;
  }
  return makeObjectVector(objectToValue, 0, totalObjects, *universe);
}

static ExprPtr createObjectLookupWithGivenContainerSet(
    int objectCount,
    int nObjectsToLookup,
    const std::shared_ptr<PackerSet<entities::ContainerId>>& containers,
    int lookupNum,
    const std::shared_ptr<const entities::Universe>& universe,
    const Assignment& assignment = Assignment{}) {
  PackerMap<entities::ObjectId, double> objectToValue;
  while (static_cast<int>(objectToValue.size()) < nObjectsToLookup) {
    const int id =
        (nObjectsToLookup * lookupNum + objectToValue.size()) % objectCount;
    objectToValue[object(id, universe)] = 1;
  }

  auto objectVector =
      makeObjectVector(objectToValue, 0, objectCount, *universe);
  return object_lookup(objectVector, containers, assignment);
}

static ExprPtr createObjectLookup(
    int objectCount,
    int containerCount,
    int nObjectsToLookup,
    int nContainersToLookup,
    int lookupNum,
    const std::shared_ptr<const entities::Universe>& universe,
    const Assignment& assignment = Assignment{}) {
  // add nContainersToLookup containers
  auto containers = std::make_shared<PackerSet<entities::ContainerId>>();
  while (static_cast<int>(containers->size()) < nContainersToLookup) {
    containers->insert(
        container((lookupNum + containers->size()) % containerCount, universe));
  }
  return createObjectLookupWithGivenContainerSet(
      objectCount,
      nObjectsToLookup,
      containers,
      lookupNum,
      universe,
      assignment);
}

static void fullApply(
    int objectCount,
    int containerCount,
    int nObjectsPerLookup,
    int nContainersPerLookup,
    std::shared_ptr<const entities::Universe> universe) {
  folly::BenchmarkSuspender suspend;
  // Create an assignment where object i is in container i%containerCount; each
  // container will have (objectCount/containerCount) objects
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  const int nLookups = 10e3;
  suspend.dismiss();

  // Create several (nLookups) lookups, each of which look at a small number of
  // objects (nObjectsPerLookup) over several containers (nContainersPerLookup)
  for (const auto lookupNum : folly::irange(nLookups)) {
    ExprPtr objectLookup;
    BENCHMARK_SUSPEND {
      objectLookup = createObjectLookup(
          objectCount,
          containerCount,
          nObjectsPerLookup,
          nContainersPerLookup,
          lookupNum,
          universe,
          assignment);
    }

    Context context;
    objectLookup->fullApply(TopToBottomEvaluator(context), assignment);
  }
}

BENCHMARK(ObjectLookupFullApply) {
  // This benchmark is to show how when 'applying' in objectLookup, it might be
  // beneficial to directly look at only the relevant objects, rather than
  // looking at the containers and then looking at the objects that are in the
  // them
  constexpr int objectCount = 500e3;
  constexpr int containerCount = 100;
  std::shared_ptr<const entities::Universe> universe;
  BENCHMARK_SUSPEND {
    universe = buildUniverse(objectCount, containerCount);
  }
  fullApply(
      objectCount,
      containerCount,
      500 /*nObjectsPerLookup*/,
      20 /*nContainersPerLookup*/,
      universe);
}

BENCHMARK(ObjectLookupFullApplyFewObjectsPerLookup) {
  // This benchmark is to show how when 'applying' in objectLookup, it might be
  // beneficial to directly look at only the relevant objects AND at the same
  // time ensure that there is no attempt to find the number of objects that are
  // in the containers (i.e., there is no call to
  // getCurrObjectsInContainersSize() in ObjectLookup innerFullApply(), since
  // doing so can be very slow when there are many containers)
  constexpr int objectCount = 1e6;
  constexpr int containerCount = 100e3;
  std::shared_ptr<const entities::Universe> universe;
  BENCHMARK_SUSPEND {
    universe = buildUniverse(objectCount, containerCount);
  }
  fullApply(
      objectCount,
      containerCount,
      100 /*nObjectsPerLookup*/,
      100e3 /*nContainersPerLookup*/,
      universe);
}

BENCHMARK(ObjectLookupPartialApplyLargeChangeSet) {
  // This benchmark is very similar to 'ObjectLookupApply' and shows how when
  // using 'partial_apply' in objectLookup, it might be beneficial to directly
  // look at only the relevant objects, rather than looking at the containers
  // and then looking at the objects that are in the them
  folly::BenchmarkSuspender suspend;
  constexpr int containerCount = 100;
  constexpr int objectCount = 500e3;
  const auto universe = buildUniverse(objectCount, containerCount);
  // Create an assignment where object i is in container i%containerCount; each
  // container will have 5000 objects
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  constexpr int nLookups = 1000;
  constexpr int nObjectsToLookup = 500;
  constexpr int nContainersToLookup = 20;

  auto changeSet = createLargeChangeSet(objectCount, containerCount, universe);
  suspend.dismiss();

  // Create several (nLookups) lookups, each of which look at a small number of
  // objects (nObjectsToLookup) over several containers (nContainersToLookup)
  for (const auto lookupNum : folly::irange(nLookups)) {
    auto objectLookup = createObjectLookup(
        objectCount,
        containerCount,
        nObjectsToLookup,
        nContainersToLookup,
        lookupNum,
        universe,
        assignment);

    // Perform an initial full_apply and then do a partial_apply with the
    // changes immediately
    Context context;
    objectLookup->fullApply(TopToBottomEvaluator(context), assignment);

    Orchestrator orchestrator;
    orchestrator.init(
        std::vector<Expression*>{objectLookup.get()},
        AffectedByChangeDecisionData(objectCount, containerCount));

    context.clear();
    context.changes() = changeSet;
    partialApply(orchestrator, objectLookup.get(), assignment, context);
  }
}

BENCHMARK(LookupPartialLargeChangeManyObjectsPerLookup) {
  folly::BenchmarkSuspender suspend;
  constexpr int containerCount = 100e3;
  constexpr int objectCount = 500e3;
  constexpr int nLookups = 1e6;
  constexpr int nObjectsPerLookup = 250e3;
  const auto universe = buildUniverse(objectCount, containerCount);

  // Create an assignment where object i is in container i%containerCount
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());
  auto objectVector =
      createObjectVector(nObjectsPerLookup, objectCount, universe);

  auto root = const_expr(0, *universe);
  for (const auto lookupNum : folly::irange(nLookups)) {
    PackerSet<entities::ContainerId> containers = {
        container(lookupNum % containerCount, universe)};
    inplace_add(
        root,
        object_lookup(
            objectVector,
            std::make_shared<PackerSet<entities::ContainerId>>(
                std::move(containers)),
            assignment));
  }

  // Perform an initial full_apply and then do a partial_apply with the
  // changes immediately
  Context context;
  root->fullApply(TopToBottomEvaluator(context), assignment);
  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{root.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));
  context.clear();
  context.changes() =
      createLargeChangeSet(objectCount, containerCount, universe);
  suspend.dismiss();

  partialApply(orchestrator, root.get(), assignment, context);
}

BENCHMARK(LookupPartialLargeChangeManyContainersPerLookup) {
  folly::BenchmarkSuspender suspend;
  constexpr int containerCount = 1e3;
  constexpr int objectCount = 50e3;
  constexpr int nLookups = 2e6;
  constexpr int nObjectsPerLookup = 10;
  const auto universe = buildUniverse(objectCount, containerCount);

  // Create an assignment where object i is in container i%containerCount
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());
  auto objectVector =
      createObjectVector(nObjectsPerLookup, objectCount, universe);

  auto allContainers = std::make_shared<PackerSet<entities::ContainerId>>();
  for (const auto i : folly::irange(containerCount)) {
    allContainers->insert(container(i, universe));
  }

  auto root = const_expr(0, *universe);
  for (const auto _ : folly::irange(nLookups)) {
    inplace_add(root, object_lookup(objectVector, allContainers, assignment));
  }

  // Perform an initial full_apply and then do a partial_apply with the
  // changes immediately
  Context context;
  root->fullApply(TopToBottomEvaluator(context), assignment);
  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{root.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));
  context.clear();
  context.changes() =
      createLargeChangeSet(objectCount, containerCount, universe);
  suspend.dismiss();

  partialApply(orchestrator, root.get(), assignment, context);
}

BENCHMARK(EvaluateSmallChangesAvoidLeavesDedupe) {
  /*This benchmark is used to show why it is useful to avoid deduplication when
   * computing changed leaves in ORchestrator when there is only one change
   */
  folly::BenchmarkSuspender suspend;
  constexpr int containerCount = 2;
  constexpr int objectCount = 5e3;
  constexpr int nLookups = 10e3;
  constexpr int nObjectsPerLookup = objectCount;
  const auto universe = buildUniverse(objectCount, containerCount);

  // Create an assignment where object i is in container i%containerCount
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());
  auto objectVector =
      createObjectVector(nObjectsPerLookup, objectCount, universe);

  auto root = const_expr(0, *universe);
  for (const auto i : folly::irange(nLookups)) {
    auto containers = std::make_shared<PackerSet<entities::ContainerId>>();
    containers->insert(container(i % containerCount, universe));
    inplace_add(
        root, object_lookup(objectVector, std::move(containers), assignment));
  }

  // Perform an initial full_apply and then do a partial_apply with the
  // changes immediately
  Context context;
  root->fullApply(TopToBottomEvaluator(context), assignment);
  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{root.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  const auto changeSet = createSmallChangeSet(
      objectCount, containerCount, /*nChanges=*/1, universe);
  constexpr int nEvals = 10e3;
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    folly::BenchmarkSuspender innerSuspend;
    context.clear();
    context.changes() = changeSet;
    innerSuspend.dismiss();

    evaluate(orchestrator, root.get(), context);
  }
}

static void partialApplyOrEvaluateManySmallChanges(
    bool usePartialApply /*if false, it evaluates*/,
    int objectCount,
    int containerCount,
    int nObjectsPerLookup,
    int nContainersPerLookup) {
  // This benchmark shows how when using 'partial_apply' in objectLookup, it
  // might be beneficial to directly look at the changeSet if it is small enough
  folly::BenchmarkSuspender suspend;
  const auto universe = buildUniverse(objectCount, containerCount);

  // Create an assignment where object i is in container i%containerCount; each
  // container will have (objectCount/ContainerCount) objects
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  constexpr int nMoves = 50e6;
  auto objectLookup = createObjectLookup(
      objectCount,
      containerCount,
      nObjectsPerLookup,
      nContainersPerLookup,
      0,
      universe,
      assignment);

  // Perform an initial full_apply and then do a partial_apply with the
  // changes immediately
  Context context;
  objectLookup->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objectLookup.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));
  suspend.dismiss();

  // Create a large (nMoves) number of small changes
  for (const auto _ : folly::irange(nMoves)) {
    ChangeSet changeSet;
    BENCHMARK_SUSPEND {
      changeSet = createSmallChangeSet(
          objectCount, containerCount, /* nChanges=*/1, universe);
    }

    context.clear();
    context.changes() = changeSet;
    usePartialApply
        ? partialApply(orchestrator, objectLookup.get(), assignment, context)
        : evaluate(orchestrator, objectLookup.get(), context);
  }
}

BENCHMARK(ObjectLookupPartialApplyManySmallChanges) {
  // This benchmark shows how when using 'partial_apply' in objectLookup, it
  // might be beneficial to directly look at the changeSet if it is small enough
  partialApplyOrEvaluateManySmallChanges(
      true /*usePartialApply*/, 5e3, 50e3, 2000, 1000);
}

BENCHMARK(ObjectLookupEvaluateManySmallChanges) {
  // This benchmark shows how when using 'evaluate' in objectLookup, it
  // might be beneficial to directly look at the changeSet if it is small enough
  partialApplyOrEvaluateManySmallChanges(
      false /*usePartialApply = false => it will use evaluate()*/,
      1e6,
      50e3,
      10e3,
      10e3);
}

BENCHMARK(LookupPartialApplyAffectedByObjects) {
  // This benchmark shows how when using 'partial_apply' and there an expression
  // that consists of many objectLookups, then it is sometimes beneficial to
  // look at affected objects instead of affected containers. This is because
  // the number of objects every lookup affects might be much smaller than the
  // number of containers it affects. (A typical example where this might happen
  // is when there is lookup w.r.t. a certain group of objects.)
  folly::BenchmarkSuspender suspend;
  constexpr int containerCount = 5e3;
  constexpr int objectCount = 50e3;
  const auto universe = buildUniverse(objectCount, containerCount);
  // Create an assignment where object i is in container i%containerCount; each
  // container will have 5000 objects
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  constexpr int nLookupExprs = 100e3;
  constexpr int nMoves = 50e3;
  constexpr int nObjectsWithNonZeroValue = 10;
  constexpr int nContainersToLookup = 100;

  auto objective = const_expr(0, *universe);
  for (const auto lookupNum : folly::irange(nLookupExprs)) {
    objective += createObjectLookup(
        objectCount,
        containerCount,
        nObjectsWithNonZeroValue,
        nContainersToLookup,
        lookupNum,
        universe,
        assignment);
  }

  // Perform an initial full_apply and then do a partial_apply with the
  // changes immediately
  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));
  suspend.dismiss();

  // Create a large (nMoves) number of small changes
  for (const auto _ : folly::irange(nMoves)) {
    auto changeSet = createSmallChangeSet(
        objectCount, containerCount, /*nChanges=*/1, universe);

    context.clear();
    context.changes() = changeSet;
    partialApply(orchestrator, objective.get(), assignment, context);
  }
}

BENCHMARK(NeedPruningLookupNodesByBothObjectAndContainers) {
  // Suppose we have M object lookups, where each Lookup is over distinct K
  // objects but the same set of N containers.
  // Indeed, if we use containers in the change set to identify the set of
  // affected leaves, we get M lookup nodes. But if we use objects to identify
  // the set of affected leaves, we only get 1 lookup node. It is not possible
  // for one lookup node to know what containers are used by other lookup nodes.
  // So, we use this rough heuristic to determine when to use objects and when
  // to use containers to compute set of affected leaves.
  //  If K < N, use objects to identify the set of affected leaves
  //  Else, use containers to identify the set of affected leaves
  // Code pointer where this is done: https://fburl.com/code/mqfkq4mw
  //
  // Recall that smaller the set of affected leaves, the more efficient are the
  // evaluate and partial apply operations
  //
  // This benchmark shows that if K > N, the above case will result in M
  // affected leaf nodes which can greatly affect performance
  folly::BenchmarkSuspender suspend;
  constexpr int containerCount = 10; // N = 10
  constexpr int numObjectsToLookup = 20; // K = 20
  constexpr int numGroups = 5e5; // M = 500k
  constexpr int objectCount = numGroups * numObjectsToLookup;
  const auto universe = buildUniverse(objectCount, containerCount);

  // add nContainersToLookup containers
  auto containers = std::make_shared<PackerSet<entities::ContainerId>>();
  for (const auto i : folly::irange(containerCount)) {
    containers->insert(container(i, universe));
  }

  constexpr int nLookupExprs = numGroups;

  // Create an assignment where object i is in container i % containerCount;
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  auto objective = const_expr(0, *universe);
  for (const auto lookupNum : folly::irange(nLookupExprs)) {
    objective += createObjectLookupWithGivenContainerSet(
        objectCount,
        numObjectsToLookup,
        containers,
        lookupNum,
        universe,
        assignment);
  }

  // Perform an initial full_apply and then do a partial_apply with the
  // changes immediately
  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));
  suspend.dismiss();

  // Create a large (nMoves) number of small changes
  // each move will touch at least M = 500k nodes, so total nodes touched is
  // at least nMoves * M
  constexpr int nMoves = 1e6;
  for (const auto _ : folly::irange(nMoves)) {
    auto changeSet = createSmallChangeSet(
        objectCount, containerCount, 1 /* nChanges */, universe);
    context.clear();
    context.changes() = changeSet;
    partialApply(orchestrator, objective.get(), assignment, context);
  }
}

BENCHMARK(EquivalentSetsComputation) {
  // This benchmark is to show how using folly::F14FastSet is beneficial for
  // "visited" map during equivalentSets computation

  constexpr int nExprs = 1e6;
  constexpr int nObjects = 1000;
  constexpr int nContainers = 1;
  // suspending here since the computation below is not relevant to what we are
  // trying to measure
  folly::BenchmarkSuspender suspend;

  std::shared_ptr<const entities::Universe> universe;
  std::vector<ExprPtr> exprs;

  universe = buildUniverse(nObjects, nContainers);
  const Assignment assignment{};
  exprs.reserve(nExprs);
  auto sum = const_expr(0, *universe);
  for (const auto _ : folly::irange(nExprs)) {
    auto objectVector = makeObjectVector(
        PackerMap<entities::ObjectId, double>{}, 1, nObjects, *universe);
    auto containersPtr = std::make_shared<PackerSet<entities::ContainerId>>(
        PackerSet<entities::ContainerId>({container(0, universe)}));
    auto objectLookup = object_lookup(objectVector, containersPtr, assignment);
    exprs.push_back(objectLookup);
  }

  EquivalenceSets equivalenceSets(*universe);
  suspend.dismiss();
  for (const auto i : folly::irange(nExprs)) {
    exprs.at(i)->updateEquivalenceSets(equivalenceSets);
  }
  suspend.rehire();
}

BENCHMARK(EquivalentSetsComputationLinearSum) {
  // This benchmark is to ensure we avoid looking at the coefficient of each
  // child of LinearSum and instead only look it up when the child is a variable
  folly::BenchmarkSuspender suspend;
  constexpr int nExprs = 1e6;
  constexpr int nObjects = 1000;
  constexpr int nContainers = 1;
  const auto universe = buildUniverse(nObjects, nContainers);
  const Assignment assignment{};
  auto sum = const_expr(0, *universe);
  std::vector<entities::ObjectId> allObjectIds;
  Orchestrator orchestrator;

  for (const auto id : folly::irange(nObjects)) {
    allObjectIds.push_back(object(id, universe));
  }
  auto containersPtr = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(0, universe)});
  for (const auto _ : folly::irange(nExprs)) {
    auto objectVector = makeObjectVector(
        PackerMap<entities::ObjectId, double>{}, 1, nObjects, *universe);
    auto objectLookup = object_lookup(objectVector, containersPtr, assignment);
    sum += objectLookup;
  }
  orchestrator.init(
      std::vector<Expression*>{sum.get()},
      AffectedByChangeDecisionData(nObjects, containersPtr->size()));
  suspend.dismiss();

  EquivalenceSets eqSets(*universe);
  orchestrator.updateEquivalenceSets(eqSets, allObjectIds.size());
  suspend.rehire();
}

BENCHMARK(EquivalenceSetsComputationEarlyStop) {
  // This benchmark is to ensure that we stop as soon as we determine that all
  // objects are unequal during equivalenceSets computation. In certain real
  // instances (e.g., in the case of Shard Manager), this can reduce the
  // time for equivalenceSets computation significantly
  constexpr int nLookups = 100;
  constexpr int nObjects = 1e6;
  constexpr int nContainers = 1;

  // Declare suspender first so it is destroyed last, ensuring destructor
  // costs of the variables below are not measured.
  folly::BenchmarkSuspender suspend;
  std::shared_ptr<const entities::Universe> universe;
  Orchestrator orchestrator;
  std::vector<entities::ObjectId> allObjectIds;
  ExprPtr sum;

  universe = buildUniverse(nObjects, nContainers);
  sum = const_expr(0, *universe);

  for (const auto id : folly::irange(nObjects)) {
    allObjectIds.push_back(object(id, universe));
  }

  PackerMap<entities::ObjectId, double> objectToValue;
  for (auto objectId : allObjectIds) {
    objectToValue[objectId] = objectId.asInt() + 1;
  }

  // create a 'nLookups' lookups affecting the same set of 'nObjects' objects,
  // but with different objectVectors
  auto containersPtr = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(0, universe)});
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  for (const auto _ : folly::irange(nLookups)) {
    sum += object_lookup(
        makeObjectVector(objectToValue, 0, nObjects, *universe),
        containersPtr,
        assignment);

    orchestrator.init(
        std::vector<Expression*>{sum.get()},
        AffectedByChangeDecisionData(nObjects, containersPtr->size()));
  }
  suspend.dismiss();

  EquivalenceSets eqSets(*universe);
  orchestrator.updateEquivalenceSets(eqSets, allObjectIds.size());
  suspend.rehire();
}

BENCHMARK(EquivalenceSetsComputationMultiVisit) {
  // Since we compute equivalence sets in a bottom-up fashion, we would not
  // visit the same node multiple times. An exception to this is StableStayed
  // which uses an object_vector V other than its child for equivalence set
  // computation. So it is possible that, the object vector V is visited
  // multiple times, and we need to ensure that we do not recompute the
  // equivalence set on each visit. This benchmark ensures that we have an
  // optimization to cache nodes that have already been visited.
  constexpr int nObjects = 1e4;
  constexpr int nContainers = 1;
  std::shared_ptr<const entities::Universe> universe;
  Orchestrator orchestrator;
  std::vector<entities::ObjectId> allObjectIds;
  ExprPtr sum;

  BENCHMARK_SUSPEND {
    universe = buildUniverse(nObjects, nContainers);
    sum = const_expr(0, *universe);
    for (const auto id : folly::irange(nObjects)) {
      allObjectIds.push_back(object(id, universe));
    }
    PackerMap<entities::ObjectId, double> objectToValue;
    for (auto objectId : allObjectIds) {
      // number of equivalent sets is at least nObjects / 2
      // ensures that all-objects unequal optimization is not triggered
      objectToValue[objectId] = objectId.asInt() / 2;
    }
    auto fullObjectVector =
        makeObjectVector(objectToValue, 0, nObjects, *universe);
    // create as many StableStayed expressions as number of objects.
    // StableStayed only splits equivalence sets based on fullObjectVector, so
    // there should be exactly one split based on fullObjectVector. This will be
    // significantly slower if we do not avoid redundant computations on
    // subsequent visits for rach stable stayed expression
    auto containersPtr = std::make_shared<PackerSet<entities::ContainerId>>(
        PackerSet<entities::ContainerId>{container(0, universe)});
    const Assignment assignment(
        universe->getContainers().getInitialAssignment());
    for (const auto id : folly::irange(nObjects)) {
      auto initialObjectVector =
          makeObjectVector({{object(id, universe), 1}}, 0, nObjects, *universe);
      sum += stable_stayed(
          initialObjectVector,
          fullObjectVector,
          containersPtr,
          *universe,
          assignment);
    }

    orchestrator.init(
        std::vector<Expression*>{sum.get()},
        AffectedByChangeDecisionData(nObjects, containersPtr->size()));
  }

  EquivalenceSets eqSets(*universe);
  orchestrator.updateEquivalenceSets(eqSets, allObjectIds.size());
}

BENCHMARK(ObjectPartitionLookupBounds) {
  // This benchmark is to ensure that bounds computation is fast in
  // ObjectPartitionLookup
  constexpr int nObjectPartitions = 10;
  constexpr int nLookupsPerPartition = 1000;
  constexpr int nObjects = 400e3;
  constexpr int nGroups = 200e3; // each group has two objects

  ExprPtr sum;

  // suspending here since the computation below is not relevant to what we are
  // trying to measure
  folly::BenchmarkSuspender suspend;
  BenchmarkUniverse builder;

  // Build initial assignment and group mappings
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  entities::Map<std::string, std::vector<std::string>> groupToObjects;

  for (const auto i : folly::irange(nObjects)) {
    const int gId = i % nGroups;
    const auto objectName = fmt::format("object{}", i);
    const auto groupName = fmt::format("group{}", gId);
    const int j = i % nLookupsPerPartition;
    const auto containerName = fmt::format("container{}", j);
    groupToObjects[groupName].push_back(objectName);
    initialAssignment[containerName].push_back(objectName);
  }

  builder.setInitialAssignment(initialAssignment);

  // Add partition and scope
  folly::coro::blockingWait(builder.addPartition("partition1", groupToObjects));
  folly::coro::blockingWait(builder.addScope(
      "scope1",
      entities::Map<std::string, std::vector<std::string>>{
          {"scopeItem0", {"container0"}}}));

  const auto partitionId = builder.partitionId("partition1");
  const auto dimensionId = builder.dimensionId("object_count");
  const auto scopeId = builder.scopeId("scope1");
  const auto scopeItem0 = builder.scopeItemId(scopeId, "scopeItem0");

  const auto universe = builder.buildUniverse();
  const Assignment assignment{};

  sum = const_expr(0, *universe);

  for (const auto _ : folly::irange(nObjectPartitions)) {
    auto objectPartition = object_partition(
        std::make_shared<const PartitionInfo>(*universe, partitionId),
        dimensionId,
        {});

    for (const auto j : folly::irange(nLookupsPerPartition)) {
      const auto containerId =
          universe->getContainerId(fmt::format("container{}", j));
      sum += object_partition_lookup(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{containerId}),
          scopeId,
          scopeItem0,
          assignment);
    }
  }

  suspend.dismiss();
  Context context;
  sum->lowerAndUpperBounds(context);
  suspend.rehire();
}

BENCHMARK(LookupEvalSparseObjectIndexed) {
  // Shared ObjectVector: 100 non-default objects out of 10K total.
  // 10000 lookups, each with 20 different containers out of 1K.
  // objectRatio (1%) < containerRatio (2%) → object-indexed.
  // All 10000 lookups share the same objects → objectToLeaves_ has 10000
  // entries per object. A move of a non-default object triggers all 10000
  // lookups, but ~98% of lookups' containers don't overlap with the change set.
  folly::BenchmarkSuspender suspend;
  constexpr int objectCount = 10e3;
  constexpr int containerCount = 1e3;
  constexpr int nLookups = 10e3;
  constexpr int nNonDefaultObjects = 100;
  constexpr int nContainersPerLookup = 20;
  constexpr int nEvals = 50e3;
  const auto universe = buildUniverse(objectCount, containerCount);
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  auto objectVector =
      createObjectVector(nNonDefaultObjects, objectCount, universe);

  auto objective = const_expr(0, *universe);
  for (const auto i : folly::irange(nLookups)) {
    auto containers = std::make_shared<PackerSet<entities::ContainerId>>();
    for (const auto j : folly::irange(nContainersPerLookup)) {
      containers->insert(
          container((i * nContainersPerLookup + j) % containerCount, universe));
    }
    inplace_add(
        objective,
        object_lookup(objectVector, std::move(containers), assignment));
  }

  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  // Construct a change that always moves a non-default object (with non-zero
  // value) so every eval triggers all 10000 lookups.
  ChangeSet changeSet;
  changeSet.insert(Change(object(1, universe), container(1, universe), -1));
  changeSet.insert(Change(object(1, universe), container(2, universe), 1));
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    context.clear();
    context.changes() = changeSet;
    evaluate(orchestrator, objective.get(), context);
  }
}

BENCHMARK(LookupEvalSparseContainerIndexed) {
  // Per-lookup ObjectVector: 1000 non-default objects per lookup out of 10K.
  // 200 lookups, each with 5 containers out of 100.
  // objectRatio (10%) > containerRatio (5%) → container-indexed.
  // Each container maps to ~10 lookups. A move triggers ~10 lookups from
  // the container index, but ~90% of moves involve objects with zero value
  // in the lookup's ObjectVector.
  folly::BenchmarkSuspender suspend;
  constexpr int objectCount = 10e3;
  constexpr int containerCount = 100;
  constexpr int nLookups = 200;
  constexpr int nObjectsPerLookup = 1000;
  constexpr int nContainersPerLookup = 5;
  constexpr int nEvals = 5e6;
  const auto universe = buildUniverse(objectCount, containerCount);
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  auto objective = const_expr(0, *universe);
  for (const auto lookupNum : folly::irange(nLookups)) {
    objective += createObjectLookup(
        objectCount,
        containerCount,
        nObjectsPerLookup,
        nContainersPerLookup,
        lookupNum,
        universe);
  }

  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  // Move a non-default object (with non-zero value) so every eval triggers
  // the container index.
  ChangeSet changeSet;
  changeSet.insert(Change(object(1, universe), container(1, universe), -1));
  changeSet.insert(Change(object(1, universe), container(2, universe), 1));
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    context.clear();
    context.changes() = changeSet;
    evaluate(orchestrator, objective.get(), context);
  }
}

BENCHMARK(LookupEvalDenseContainerIndexed) {
  folly::BenchmarkSuspender suspend;
  constexpr int objectCount = 10e3;
  constexpr int containerCount = 100;
  constexpr int nLookups = 200;
  constexpr int nContainersPerLookup = 5;
  constexpr int nEvals = 1e6;
  const auto universe = buildUniverse(objectCount, containerCount);
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  auto objective = const_expr(0, *universe);
  for (const auto lookupNum : folly::irange(nLookups)) {
    objective += createObjectLookup(
        objectCount,
        containerCount,
        objectCount,
        nContainersPerLookup,
        lookupNum,
        universe);
  }

  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  ChangeSet changeSet;
  changeSet.insert(Change(object(1, universe), container(1, universe), -1));
  changeSet.insert(Change(object(1, universe), container(2, universe), 1));
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    context.clear();
    context.changes() = changeSet;
    evaluate(orchestrator, objective.get(), context);
  }
}

BENCHMARK(LinearSumEvalAllCoeffsOne) {
  // Benchmark LinearSum::evaluate with many cheap Variable children.
  // A single move triggers all children. All coefficients are 1.0 →
  // exercises the allCoeffsOne_ fast path.
  folly::BenchmarkSuspender suspend;
  constexpr int objectCount = 1e3;
  constexpr int containerCount = 10;
  constexpr int nChildren = 10e3;
  constexpr int nEvals = 10e3;
  const auto universe = buildUniverse(objectCount, containerCount);
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  // All children are Variable(object0, container0) — same variable repeated
  auto objective = const_expr(0, *universe);
  for (const auto _ : folly::irange(nChildren)) {
    inplace_add(
        objective,
        variable(
            object(0, universe),
            container(0, universe),
            *universe,
            assignment));
  }

  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  // Move object 0 between containers → triggers all nChildren children
  ChangeSet changeSet;
  changeSet.insert(Change(object(0, universe), container(0, universe), -1));
  changeSet.insert(Change(object(0, universe), container(1, universe), 1));
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    context.clear();
    context.changes() = changeSet;
    evaluate(orchestrator, objective.get(), context);
  }
}

BENCHMARK(LinearSumEvalMixedCoeffs) {
  // A fraction of children have non-1.0 coefficients.
  folly::BenchmarkSuspender suspend;
  constexpr int objectCount = 1e3;
  constexpr int containerCount = 10;
  constexpr int nChildren = 10e3;
  constexpr int nEvals = 10e3;
  constexpr double fractionNonOne = 0.5;
  const auto universe = buildUniverse(objectCount, containerCount);
  auto assignment =
      Assignment(universe->getContainers().getInitialAssignment());

  auto objective = const_expr(0, *universe);
  for (const auto i : folly::irange(nChildren)) {
    const double coef =
        (i < static_cast<int>(nChildren * fractionNonOne)) ? i : 1.0;
    inplace_add(
        objective,
        variable(
            object(0, universe), container(0, universe), *universe, assignment),
        coef);
  }

  Context context;
  objective->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{objective.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  ChangeSet changeSet;
  changeSet.insert(Change(object(0, universe), container(0, universe), -1));
  changeSet.insert(Change(object(0, universe), container(1, universe), 1));
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    context.clear();
    context.changes() = changeSet;
    evaluate(orchestrator, objective.get(), context);
  }
}

BENCHMARK(ObjectPartitionLookupEvaluateManyGroups) {
  // One ObjectPartitionLookup over a partition with many groups. Each eval of a
  // change reads the old penalty of every changed group. For the default policy
  // that read used to be an O(log n) SumMap lookup and is now an O(1)
  // recompute. We move many objects per eval so a single eval touches many
  // groups, so the SumMap lookup dominates and the two versions differ starkly.
  folly::BenchmarkSuspender suspend;
  constexpr int nGroups = 200e3;
  constexpr int objectsPerGroup = 2;
  constexpr int objectCount = nGroups * objectsPerGroup;
  constexpr int containerCount = 10;
  constexpr int nMovedObjects = 20e3;
  constexpr int nEvals = 2500;

  BenchmarkUniverse builder;
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  entities::Map<std::string, std::vector<std::string>> groupToObjects;
  std::vector<std::string> allContainerNames;
  for (const auto i : folly::irange(containerCount)) {
    allContainerNames.push_back(fmt::format("container{}", i));
    initialAssignment[fmt::format("container{}", i)] = {};
  }
  for (const auto i : folly::irange(objectCount)) {
    const auto objectName = fmt::format("object{}", i);
    groupToObjects[fmt::format("group{}", i % nGroups)].push_back(objectName);
    initialAssignment[fmt::format("container{}", i % containerCount)].push_back(
        objectName);
  }
  builder.setInitialAssignment(initialAssignment);
  folly::coro::blockingWait(builder.addPartition("partition1", groupToObjects));
  folly::coro::blockingWait(builder.addScope(
      "scope1",
      entities::Map<std::string, std::vector<std::string>>{
          {"scopeItem0", allContainerNames}}));

  const auto partitionId = builder.partitionId("partition1");
  const auto dimensionId = builder.dimensionId("object_count");
  const auto scopeId = builder.scopeId("scope1");
  const auto scopeItem0 = builder.scopeItemId(scopeId, "scopeItem0");
  const auto universe = builder.buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partitionId),
      dimensionId,
      {});
  auto containers = std::make_shared<PackerSet<entities::ContainerId>>();
  for (const auto i : folly::irange(containerCount)) {
    containers->insert(container(i, universe));
  }
  auto lookup = object_partition_lookup(
      objectPartition, containers, scopeId, scopeItem0, assignment);

  Context context;
  lookup->fullApply(TopToBottomEvaluator(context), assignment);

  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{lookup.get()},
      AffectedByChangeDecisionData(objectCount, containerCount));

  // Move many objects, each to a neighbouring container, so a single eval's
  // change set spans many groups.
  ChangeSet changeSet;
  for (const auto k : folly::irange(nMovedObjects)) {
    changeSet.insert(Change(
        object(k, universe), container(k % containerCount, universe), -1));
    changeSet.insert(Change(
        object(k, universe), container((k + 1) % containerCount, universe), 1));
  }
  suspend.dismiss();

  for (const auto _ : folly::irange(nEvals)) {
    context.clear();
    context.changes() = changeSet;
    evaluate(orchestrator, lookup.get(), context);
  }
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
