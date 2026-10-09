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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"

#include <folly/container/F14Set.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace facebook::rebalancer {

class Problem;
class Expression;

class EquivalenceSetsStore {
 public:
  explicit EquivalenceSetsStore(Problem& problem);

  /** Initializes the equivalence sets using the custom config
   */
  void initialize(const interface::CustomEquivalenceSetConfig& config);

  /**
   * Initializes the equivalence set using a subtree of all goals and
   * constraints, except the ones specified in `excludeConstraintNames` and
   * `excludeGoalNames`. The set computed without any exclusions is most
   * granular.
   * @param excludeConstraintNames Names of the constraints to be excluded from
   * equivalence set partitioning logic.
   * @param excludeGoalNames Names of the goals to be excluded from
   * equivalence set partitioning logic.
   */
  void initialize(
      const folly::F14FastSet<std::string>& excludeConstraintNames,
      const folly::F14FastSet<std::string>& excludeGoalNames);

  /**
   * Initializes the equivalence sets for the specified search space partition,
   * constraints and goals.
   * @param searchSpacePartitionName Name of the search space partition.
   * @param constraintNames Names of the constraints to be considered.
   * @param goalNames Names of the goals to be considered.
   */
  void initialize(
      const std::string& searchSpacePartitionName,
      const std::vector<std::string>& constraintNames,
      const std::vector<std::string>& goalNames);

  /** Overrides the equivalence set in the store (if it exists) with the same
   * key as the one provided in @param equivalenceSets  */
  void override(EquivalenceSets equivalenceSets);

  /**
   * Clears the equivalence sets calculated in the last set() call.
   */
  void clear();

  /**
   * Returns the equivalence sets calculated in the last initialize() call.
   * If initialize() was never called, it initializes and returns the default
   * equivalence set (one with all goals and constraints)
   * @return A constant reference to the EquivalenceSets object.
   */
  const EquivalenceSets& get() const;

  // returns the total time spent in building equivalence sets (in seconds)
  double getTotalBuildTime() const;

 private:
  Problem& problem_;
  // unique_ptr because EquivalenceSets has no default ctor
  materializer::Cache<std::string, std::unique_ptr<EquivalenceSets>>
      equivalenceSetsCache_;

  // Pointer to the equivalence sets that was initialized in the last
  // initialize() call. The map equivalenceSetsCache_ stores the sets for all
  // previously initialized equivalence set configs. For quick access via get(),
  // this pointer will store to the address of most recently initialized
  // equivalence set currently in the cache.
  const EquivalenceSets* mostRecentEquivSetsPtr_ = nullptr;

  double totalEquivSetsBuildTime_ = 0;

  std::vector<entities::ConstraintId> allConstraintIds_;
  std::vector<entities::GoalId> allGoalIds_;

  /**
   * Builds and stores the equivalence sets using the specified partition,
   * constraints and goals.
   * @param constraintIds Ids of the constraints to be included.
   * @param goalIds Ids of the goals to be included.
   * @param partitionIds Ids of partition to be included.
   */
  void buildAndStoreInclusive(
      const std::vector<entities::ConstraintId>& constraintIds,
      const std::vector<entities::GoalId>& goalIds,
      const std::vector<entities::PartitionId>& partitionIds,
      const std::string& cacheKey);

  /**
   * Builds and stores the equivalence sets ecluding the specified constraints
   * and goals.
   * @param excludeConstraintNames Ids of the constraints to be included.
   * @param excludeGoalNames Ids of the goals to be included.
   */
  void buildAndStoreExclusive(
      const entities::Set<entities::ConstraintId>& excludedConstraintIds,
      const entities::Set<entities::GoalId>& excludedGoalIds,
      const std::string& cacheKey);

  /** Returns the default (most-granular) equivalence sets, the one with all
   * the goals and constraints. This is slightly more optimized than using the
   * buildExclusive({}, {}) because we can reuse the pre-initialized
   * orchestrator */
  EquivalenceSets buildDefaultEquivalenceSets();

  /** stores the @param equivalenceSets in the cache indexed by their key */
  void saveToEquivalenceSetCache(EquivalenceSets equivalenceSets);

  /** updates the @param mostRecentEquivSetsPtr_ to point to the one stored in
   * the cache indexed by @param key */
  void updateMostRecentEquivSetsPtr(const std::string& key);

  /** update the @param equivalenceSets using the expressions corresponding to
   * specified constraint and goals*/
  void updateEquivalenceSets(
      EquivalenceSets& equivalenceSets,
      const std::vector<entities::ConstraintId>& constraintIds,
      const std::vector<entities::GoalId>& goalIds);

  void buildCustomEquivalenceSets(
      const std::vector<Expression*>& roots,
      const Problem& problem,
      EquivalenceSets& equivalenceSets);

  // initializes equivalence sets with the defaultKey
  void buildAndStoreDefault();
};

} // namespace facebook::rebalancer
