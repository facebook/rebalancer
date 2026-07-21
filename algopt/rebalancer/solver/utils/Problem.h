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

#pragma once

#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/solvers/LPStore.h"
#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"
#include "algopt/rebalancer/solver/summary/LabeledConstraints.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/EntityAttributesStore.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSetsStore.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/SimilarContainers.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/CppAttributes.h>

namespace facebook::rebalancer {

// If we partition the problem expression-tree variables into subproblems,
// we can have variables that are not resolved (because they are complicating).
// Depending on the partitioning approach, they need to be handled differently:
//  * for partitonHeuristic approach, they need to be part of every subproblem
//  * for partitioned LP store, a more careful approach is needed
// Therefore, we assign these variables to special subproblem. Index Zero is
// chosen because subproblem partitionIds are guaranteed to be >= 1, and also
// because partitionHeuristic over variables with partition zero in Gurobi makes
// them part of of all subproblems.
constexpr int SPECIAL_SUBPROBLEM_ID = 0;

class DestinationsToExploreGenerator;
class ObjectsToExploreGenerator;
class OptimalSolver;
class LocalSearchSolver;
class Problem {
  // Declared first so it is destroyed last, after orchestrator_/objective,
  // which hold non-owning pointers to the Universe.
  const std::shared_ptr<const rebalancer::entities::Universe> universe_;

 public:
  explicit Problem(
      std::shared_ptr<const entities::Universe> universe,
      const std::shared_ptr<const MaterializedProblem> materializedProblem,
      const ProblemConfigs& configs = ProblemConfigs(),
      std::shared_ptr<RebalancerLog> logger =
          std::make_shared<RebalancerLog>());

  friend class OptimalSolver;
  friend class LocalSearchSolver;
  friend class LocalSearchStageSolver;

  MoveResult apply(const ChangeSet& changes);

  const std::string& containerName(entities::ContainerId containerId) const;

  const std::string& objectName(entities::ObjectId objectId) const;

  entities::ContainerId containerId(const std::string& containerName) const;

  entities::ObjectId objectId(const std::string& objectName) const;

  const std::vector<entities::ContainerId>&
  getDescendingHotnessContainersOverride() const;

  Orchestrator& getOrchestrator();

  GlobalObjective objective;

  std::shared_ptr<Expression> constraint;

  Assignment assignment;

  Assignment initial_assignment;

  Context apply_context;

  Context initial_context;

  // TODO:: remove 'containers' and instead make universe return a set of
  // containers
  PackerSet<entities::ContainerId> containers;

  PackerSet<entities::ContainerId> not_accepting_containers;
  PackerSet<entities::ObjectId> fixed_objects;

  ProblemConfigs configs;

  // using the assignment of equivalence sets to containers, compute the object
  // to container assignment, making sure that the assignment is stable (i.e
  // consistent with the initial assignment)
  PackerMap<entities::ObjectId, entities::ContainerId>
  getStableObjectToContainerAssignment(
      const PackerMap<
          entities::EquivalenceSetId,
          PackerMap<entities::ContainerId, int>>&
          equivSetToContainerToObjectCount,
      const Assignment& initialAssignment,
      const std::optional<std::string>& shuffleSeed = std::nullopt);

  void log_objective_summary();

  // TODO: remove access to labeled constraints and objectives
  const LabeledConstraints& getLabeledConstraints() const;
  const GlobalLabeledObjectives& getLabeledObjectives() const;

  LPStore lp_store;
  algopt::lp::Expression lp_assignment_var(
      LpContext& context,
      entities::EquivalenceSetId equiv_set,
      entities::ContainerId container) const;

  algopt::lp::Variable assignment_var(
      entities::EquivalenceSetId equiv_set,
      entities::ContainerId container) const;

  std::optional<int> get_maybe_fixed_assignment_value(
      LpContext& context,
      entities::EquivalenceSetId equiv_set,
      entities::ContainerId container) const;

  SimilarContainers& getSimilarContainers();

  const PackerSet<entities::ContainerId>& getFixedContainers() const;

  // User may provide a scope to partition containers (and corresponding LP
  // variables). Using that scope, we obtain a partitioning of containers
  folly::Optional<PackerMap<entities::ContainerId, int>>
      containerToSubproblemId;
  // the following is inferred from above but precomputed for quick access
  PackerMap<int, PackerSet<entities::ContainerId>> subproblemToContainerIds;
  // mapping of subproblem id to names, empty if unspecified
  PackerMap<int, std::string> subproblemIdToName;

  // given containerId, get the corresponding subproblem. Default returns
  // SPECIAL_SUBPROBLEM_ID
  int getContainerSubproblemId(entities::ContainerId containerId) const;

  const PackerMap<entities::ContainerId, int>* FOLLY_NULLABLE
  getContainerToSubproblemIdsPtr() const;

  std::shared_ptr<const MoveStatsAggregatorConfig> moveStatsAggregatorConfig;

  // Returns the list of containers outside of the given scope.
  const PackerSet<entities::ContainerId>& getOutOfScopeContainerIds(
      const std::string& scopeName);

  // Returns the corresponding group given a partition and object.
  std::optional<entities::GroupId> getOnlyGroupIdIfExists(
      const std::string& partitionName,
      entities::ObjectId objectId) const;

  std::optional<entities::GroupId> getOnlyGroupIdIfExists(
      entities::PartitionId partitionId,
      entities::ObjectId objectId) const;

  // Returns the list of objects given a partition and group.
  const std::vector<entities::ObjectId>& getObjectIdsForGroup(
      const std::string& partitionName,
      entities::GroupId groupId) const;

  ObjectStore getDynamicObjects(
      entities::ContainerId containerId,
      const std::string& partitionName,
      entities::GroupId groupId) const;

  /** compute the equivalent sets induced by dynamic (movable) objects in @param
   * containers */
  PackerSet<entities::EquivalenceSetId> getDynamicEquivalentSets(
      const PackerSet<entities::ContainerId>& containers) const;

  const std::shared_ptr<const entities::Universe>& getUniversePtr() const;

  const entities::Universe& getUniverse() const;

  const std::shared_ptr<const MaterializedProblem> getMaterializedProblem()
      const;

  std::vector<const Expression*> getHardConstraintComponents(
      const std::vector<std::string>& constraintNames) const;

  const InvalidMoveFilter* getInvalidMoveFilter() const {
    return invalidMoveFilter_;
  }

  DestinationsToExploreGenerator& getDestinationsGenerator();

  ObjectsToExploreGenerator& getObjectsGenerator();

  std::shared_ptr<algopt::treeprof::ExecutorWrapper> getExecutorForLpBuilding()
      const;

  std::shared_ptr<algopt::treeprof::ExecutorWrapper> getExecutor() const;

  // this util API and related class helps with building and maintaining
  // dynamic entity attributes
  EntityAttributesStore& getEntityAttributeStore();

  // this Util API and related class helps with building and maintaining
  // equivalence sets. One can initialize custom equivalence sets using
  // getEquivalenceSetsStore().initialize(...) calls and then use
  // getEquivalenceSets() to get the equivalence sets that was initialized most
  // recently. Internally, EquivalenceSetsStore maintains a cache of equivalence
  // sets so subsequent initialize(...) calls are cheaper.
  EquivalenceSetsStore& getEquivalenceSetsStore();

  // return the equivalence sets that was initialized most recently with a
  // getEquivalenceSetsStore().initialize(...). If no such call was made, it
  // will initialize the default equivalence set with all goals and constraints
  // and return it.
  const EquivalenceSets& getEquivalenceSets() const;

  // translates the equivalence sets returned by getEquivalenceSets() to the
  // thrift format that is returned by the solver
  interface::EquivalenceSetInfo makeEquivalenceSetInfo() const;

 private:
  void log_node_summary(size_t numFixedNodes, size_t nodeCount);

  void buildInitialAssignment();

  // identifies the set of fixed nodes based on the set of fixed containers
  void identifyFixedNodes(
      Expression* node,
      folly::F14FastSet<Expression*>& visitedNodes,
      int& numFixedNodes);

  std::shared_ptr<const MoveStatsAggregatorConfig> makeMoveStatsConfig() const;

  void partialApply(const ChangeSet& changes);

  bool shouldCollectMoveStats() const;

  Orchestrator orchestrator_;

  // thread-safe
  materializer::Cache<std::string, PackerSet<entities::ContainerId>>
      scopeNameToOutOfScopeContainerIds_;

  const std::shared_ptr<const MaterializedProblem> materializedProblem_;
  const InvalidMoveFilter* invalidMoveFilter_{nullptr};

  std::optional<SimilarContainers> similarContainers;

  std::shared_ptr<DestinationsToExploreGenerator> destinationsGenerator_;

  std::shared_ptr<ObjectsToExploreGenerator> objectsGenerator_;

  std::shared_ptr<RebalancerLog> logger_;

  std::shared_ptr<algopt::treeprof::ExecutorWrapper> wrappedExecutor_;
  EntityAttributesStore entityAttributesStore_;
  EquivalenceSetsStore equivalenceSetsStore_;
};

} // namespace facebook::rebalancer
