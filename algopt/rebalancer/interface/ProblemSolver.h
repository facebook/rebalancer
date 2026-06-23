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

#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/interface/ObjectCountExpander.h"
#include "algopt/rebalancer/interface/ProblemChecker.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/interface/ThriftStrategyBuilder.h"
#include "algopt/rebalancer/interface/UniverseProblemBuilder.h"
#include <algopt/rebalancer/algopt_common/Concepts.h>
#include <algopt/rebalancer/algopt_common/Timer.h>
#include <algopt/rebalancer/algopt_common/Utils.h>

#include <folly/CancellationToken.h>
#include <folly/container/F14Map.h>
#include <folly/coro/AsyncScope.h>
#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/futures/Future.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>

namespace facebook::rebalancer::interface {

// Constraint specs whose addConstraint() takes only the spec itself (no
// policy/invalidCost/tuplePosIfBroken). Uses remove_cvref_t so the trait
// matches regardless of how the type is qualified at the call site.
template <typename Spec>
inline constexpr bool isSingleParameterConstraint =
    std::same_as<std::remove_cvref_t<Spec>, interface::AvoidMovingSpec> ||
    std::same_as<std::remove_cvref_t<Spec>, interface::MoveGroupSpec> ||
    std::same_as<std::remove_cvref_t<Spec>, interface::MovesInProgressSpec>;

// Matches iterable<(string, iterable<(string, int64_t)>)> representing
// container -> object -> count, e.g. map<string, map<string, int64_t>>.
template <typename CompactAssignmentInput>
concept IsCompactAssignmentInput = requires {
  requires IsIterableOverPairs<
      CompactAssignmentInput,
      std::string,
      typename CompactAssignmentInput::value_type::second_type>;
  requires IsIterableOverPairs<
      typename CompactAssignmentInput::value_type::second_type,
      std::string,
      int64_t>;
};

// Manages asynchronous Manifold uploads on a background thread pool.
// ProblemSolver enqueues upload coroutines via add(); the caller must
// call wait() to block until all uploads complete. If wait() is not called,
// then the destructor blocks until all uploads complete.
class AsyncManifoldUploadHandle {
 public:
  explicit AsyncManifoldUploadHandle();
  explicit AsyncManifoldUploadHandle(
      std::shared_ptr<folly::ThreadPoolExecutor> executor);

  AsyncManifoldUploadHandle(AsyncManifoldUploadHandle&&) = delete;
  AsyncManifoldUploadHandle& operator=(AsyncManifoldUploadHandle&&) = delete;
  AsyncManifoldUploadHandle(const AsyncManifoldUploadHandle&) = delete;
  AsyncManifoldUploadHandle& operator=(const AsyncManifoldUploadHandle&) =
      delete;

  ~AsyncManifoldUploadHandle();

  // Wait for upload to complete (blocking).
  void wait();

 protected:
  void add(folly::coro::Task<void> task);

 private:
  friend class ProblemSolver;

  folly::coro::AsyncScope scope_{};
  std::shared_ptr<folly::ThreadPoolExecutor> executor_;
  bool joined_{false};
};

// NOTE: All non-const, non-static member functions (except solve()) must
// include REBALANCER_PROBLEM_SETUP_TIMER_SCOPE() at the beginning. This
// ensures timeOutsideSolve_ accurately tracks the time outside solve().
#define REBALANCER_PROBLEM_SETUP_TIMER_SCOPE()           \
  [[maybe_unused]] const algopt::TimerScope timerScope { \
    problemSetupTime_                                    \
  }

bool shouldBackup(
    const ManifoldBackupParams& manifoldBackupParams,
    const ProblemProfile& profile);

class ProblemSolver {
 public:
  explicit ProblemSolver(
      std::shared_ptr<folly::ThreadPoolExecutor> executor,
      std::string serviceName,
      std::string serviceScope,
      bool prepareProblemOnly = false,
      bool canExecuteAsync = false);

  ~ProblemSolver();

  // Delete copy constructor and copy assignment to prevent copying
  ProblemSolver(const ProblemSolver&) = delete;
  ProblemSolver& operator=(const ProblemSolver&) = delete;

  // Explicitly default move constructor and move assignment
  ProblemSolver(ProblemSolver&&) = delete;
  ProblemSolver& operator=(ProblemSolver&&) = delete;

  // Set the human-readable label for objects (e.g. "task", "shard"); used
  // in logging output and the solution summary. Has no effect on solving.
  ProblemSolver& setObjectName(const std::string& objectName);

  // Set the human-readable label for containers (e.g. "host", "rack"); used
  // in logging output and the solution summary. Has no effect on solving.
  ProblemSolver& setContainerName(const std::string& containerName);

  // Restrict the search space so each object is moved at most once across
  // the entire solve.
  ProblemSolver& enableRestrictMovingObjectOnlyOnce();

  // Store partition-backed dynamic dimensions in group-keyed form: O(groups)
  // instead of O(objects). Default is expanded object-keyed storage. Must be
  // called before addDynamicObjectDimension() calls.
  ProblemSolver& setGroupBackedDynamicDimensions(bool enable);

  // This function enables an internal optimization called `StableStayed`.
  // Essentially, this optimization reduces the number of equivalence sets
  // created by the solver. This optimization is only triggered if complex
  // definition of utilization are used such as CapacitySpec::NEW definition or
  // MinimizeMovementSpec or CapacitySpec::AFTER definition.
  //
  // PRO: This optimization IMPROVES solve times by
  //  * Reducing size of generated MIP model (if using OptimalSolver)
  //  * Reducing search space of local search (if using LocalSearchSolver)
  //
  // CON: This optimization may cause following unwanted behavior
  //  * If used with optimal solver, it
  //  * If used with local search, it may not be able to UNDO moves
  //   - This happens because object that has moved once is considered
  //     equivalent to object that has not moved, so solver can choose either of
  //     them arbitrarily.
  //
  // Recommendation: Do enable if using Optimal Solver; If using local search
  // enable only if you don't care much about "undoing" sub-optimal moves.
  ProblemSolver& enableStableAsMuchAsPossible();

  // Enable per-move statistics collection with default settings.
  ProblemSolver& enableMoveStats();

  // Enable per-move statistics collection using the supplied spec.
  ProblemSolver& enableMoveStats(MoveStatsSpec spec);

  // Enable Tupperware-aware move validation; rejects moves that would
  // violate Tupperware-side invariants.
  ProblemSolver& enableMoveValidator(const TupperwareMoveValidatorSpec& spec);

  // Enable the hierarchical solver profiler (treeprof). Profiling data is
  // emitted via XLOG.
  ProblemSolver& enableProfiler();

  // Set the LP/MIP feasibility tolerance for the optimal solver backends.
  ProblemSolver& setFeasibilityTolerance(double feasibilityTolerance);
  // Optionally pass an AsyncManifoldUploadHandle to make uploads async.
  // The caller must call handle->wait() to wait for uploads to complete.
  // Otherwise, it waits in the destructor.
  ProblemSolver& setManifoldBackupParams(
      const ManifoldBackupParams& params = ManifoldBackupParams(),
      std::shared_ptr<AsyncManifoldUploadHandle> manifoldUploadHandle =
          nullptr);
  // Tag the run with a free-form label that appears in log lines and Scuba
  // rows alongside the auto-generated run id.
  ProblemSolver& setLoggingLabel(const std::string& loggingLabel);

  // Override the auto-generated run identifier. Useful for tying a solve
  // to an external trace/request id.
  ProblemSolver& setRunId(std::string runId);

  // Set the default ConstraintPolicy applied to constraints that don't
  // specify one explicitly. Defaults to ConstraintPolicy::DEFAULT.
  ProblemSolver& setConstraintPolicy(ConstraintPolicy policy);

  // Set the default ConstraintParams (invalid_cost / invalid_state) applied
  // when a constraint is added without per-constraint overrides.
  ProblemSolver& setDefaultConstraintParams(
      const ConstraintParams& constraintParams);

  // Register routing options that constrain which scope items a partition's
  // groups may be routed to during a routing-aware solve.
  ProblemSolver& addDestinationsToExploreOptions(
      const std::string& name,
      interface::DestinationsToExploreOptions destinationsToExploreOptions);

  /**
    ContainerToObjects is an iterable over a (string, vector<string>) pair,
    e.g.,: folly::F14FastMap<std::string, vector<std::string>>
           std::map<std::string, vector<std::string>>
           std::vector<std::pair<std::string, vector<std::string>>>
  */
  template <typename ContainerToObjects>
  ProblemSolver& setAssignment(const ContainerToObjects& containerToObjects)
    requires IsIterableOverPairs<
        ContainerToObjects,
        std::string,
        std::vector<std::string>>;

  // Explicit Thrift overload for callers that already have counted
  // assignments materialized.
  ProblemSolver& setAssignment(const ContainerAssignment& compactAssignment);

  // Compact-assignment overload: container -> object -> count.
  // Accepts data in ContainerAssignment shape and expands it into synthetic
  // per-object names internally.
  // MUST be called before any dimensions, partitions, or specs are added.
  template <typename CompactAssignmentInput>
  ProblemSolver& setAssignment(
      const CompactAssignmentInput& compactAssignmentInput)
    requires IsCompactAssignmentInput<CompactAssignmentInput>;

  // Override the solver's container "hotness" ranking. Currently consumed
  // only by OptimalSubsetSolver to bias subset selection.
  ProblemSolver& overrideContainerHotnessRanking(
      const std::vector<std::string>& descendingHotnessContainers);

  /**
    ScopeItemToObjectToValue is a map of the following form:
        map1<std::string, map2<std::string, double>>
        (e.g., map1 = folly::F14FastMap, map2 = folly::F14ValueMap)
  */
  template <typename ScopeItemToObjectToValue>
  ProblemSolver& addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      ScopeItemToObjectToValue scopeItemToObjectToValue,
      double defaultValue = 0)
    requires IsMapOfMap<
        ScopeItemToObjectToValue,
        std::string,
        std::string,
        double>;

  /**
      ScopeItemToGroupToValue is a map of the following form:
          map1<std::string, map2<std::string, double>>
          (e.g., map1 = folly::F14FastMap, map2 = folly::F14ValueMap)

      The value in scopeItemToGroupToValue is the value of the dimension for
      every object in the group. So, for example,
      scopeItemToGroupToValue["host1"]["group1"] = 10 means that all objects in
      group1 have a value of 10 for the dimension in host1.
    */
  template <typename ScopeItemToGroupToValue>
  ProblemSolver& addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      const std::string& partitionName,
      ScopeItemToGroupToValue scopeItemToGroupToValue,
      double defaultValue = 0)
    requires IsMapOfMap<
        ScopeItemToGroupToValue,
        std::string,
        std::string,
        double>;

  /**
    ObjectToGroup is an iterable over a (string, string) pair, e.g.,:
        folly::F14FastMap<std::string, string>
        std::map<std::string, string>
        std::vector<std::pair<std::string, string>>
  */
  template <typename ObjectToGroup>
  ProblemSolver& addPartition(
      const std::string& partitionName,
      ObjectToGroup objectToGroup)
    requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>;

  /**
    GroupToObjects is a map of the following form:
        map<std::string, vector<string>>
        (e.g., map = folly::F14FastMap or std::map or std::unordered_map)
  */
  template <typename GroupToObjects>
  ProblemSolver& addPartition(
      const std::string& partitionName,
      GroupToObjects groupToObjects)
    requires IsIterableOverPairs<
        GroupToObjects,
        std::string,
        std::vector<std::string>>;

  /**
      ObjectToValue is an iterable over a (string, double) pair, e.g.,:
          folly::F14FastMap<std::string, double>
          std::map<std::string, double>
          std::vector<std::pair<std::string, double>>
    */
  template <typename ObjectToValue, typename ScaleByUsageMap = ObjectToValue>
  ProblemSolver& addObjectDimension(
      const std::string& dimensionName,
      ObjectToValue objectToValue,
      double defaultValue = 0,
      std::optional<ScaleByUsageMap> scaleByUsageMap = std::nullopt)
    requires IsIterableOverPairs<ObjectToValue, std::string, double> &&
      IsIterableOverPairs<ScaleByUsageMap, std::string, double>;

  /**
      ObjectToValues is a map of the following form:
          map<std::string, vector<double>>
          (e.g., map = folly::F14FastMap or std::map or std::unordered_map)
    */
  template <typename ObjectToValues>
  ProblemSolver& addObjectDimension(
      const std::string& dimensionName,
      ObjectToValues objectToValues,
      double defaultValue = 0)
    requires IsIterableOverPairs<
        ObjectToValues,
        std::string,
        std::vector<double>>;

  // Define an object dimension whose value depends on routing decisions
  // made by the named routing config. Each partition group has a base
  // value used when the routing places it.
  ProblemSolver& addObjectPartitionRoutingDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      double defaultValue = 0);

  // Routing-aware object dimension that combines a routing-dependent value
  // (groupToValue) with a static, routing-independent baseline
  // (groupToStaticValue) per partition group.
  ProblemSolver& addObjectPartitionRoutingDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      const std::unordered_map<std::string, double>& groupToStaticValue,
      double defaultValue = 0, // default value for groupToValue
      double defaultStaticValue = 0); // default value for groupToStaticValue

  /**
    ContainerToValue is an iterable over a (string, double) pair, e.g.,:
        folly::F14FastMap<std::string, double>
        std::map<std::string, double>
        std::vector<std::pair<std::string, double>>
  */
  template <typename ContainerToValue>
  ProblemSolver& addContainerDimension(
      const std::string& dimensionName,
      const ContainerToValue& containerToValue,
      double defaultValue = 1.0)
    requires IsIterableOverPairs<ContainerToValue, std::string, double>;

  /**
      ContainerToScopeItem is an iterable over a (string, string) pair, e.g.,:
          folly::F14FastMap<std::string, string>
          std::map<std::string, string>
          std::vector<std::pair<std::string, string>>
    */
  template <typename ContainerToScopeItem>
  ProblemSolver& addScope(
      const std::string& scopeName,
      const ContainerToScopeItem& containerToScopeItem)
    requires IsIterableOverPairs<ContainerToScopeItem, std::string, std::string>
  ;

  /**
      ScopeItemToContainers is a map of the following form:
          map<std::string, vector<string>>
          (e.g., map = folly::F14FastMap or std::map or std::unordered_map)
    */
  template <typename ScopeItemToContainers>
  ProblemSolver& addScope(
      const std::string& scopeName,
      const ScopeItemToContainers& scopeItemToContainers)
    requires IsIterableOverPairs<
        ScopeItemToContainers,
        std::string,
        std::vector<std::string>>;

  /**
    ScopeItemToValue is an iterable over a (string, double) pair, e.g.,:
        folly::F14FastMap<std::string, double>
        std::map<std::string, double>
        std::vector<std::pair<std::string, double>>
    */
  template <typename ScopeItemToValue>
  ProblemSolver& addScopeDimension(
      const std::string& dimensionName,
      const std::string& scopeName,
      const ScopeItemToValue& scopeItemToValue,
      double defaultValue = 1.0)
    requires IsIterableOverPairs<ScopeItemToValue, std::string, double>;

  // Declare equivalence classes of containers; the solver may treat
  // containers within a class as interchangeable for symmetry-breaking
  // and search-space reduction. Each inner vector is one class.
  ProblemSolver& addSimilarContainers(
      const std::vector<std::vector<std::string>>& similarContainerClasses);

  // Register a routing configuration referenced by
  // addObjectPartitionRoutingDimension*. The routing rings drive how
  // partition groups can be placed across scope items, and the latency
  // table powers routing-cost calculations.
  ProblemSolver& addRoutingConfig(
      const std::string& configName,
      const std::string& scopeName,
      const std::string& partitionName,
      const std::unordered_map<std::string, GroupRoutingRings>&
          groupToRoutingRings,
      const std::unordered_map<
          std::string,
          std::unordered_map<std::string, double>>& originToDestinationLatency,
      const std::optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>&
          defaultOriginToDestinationScopeItemSets = std::nullopt);

  // Advance to the next tuple position. Subsequent addGoal calls form a
  // strictly lower-priority bucket in the lexicographic objective.
  ProblemSolver& addGoalBoundary();

  // Return the current tuple position index used by addGoal.
  int getCurrentGoalIndex();

  /**
    Spec is any spec in ProblemSolverThrift.GoalSpecs
  */
  template <typename Spec>
  ProblemSolver& addGoal(
      Spec spec,
      double weight = 1,
      std::optional<int> tuplePos = std::nullopt)
    requires FieldTypeExistsInThriftStructOrUnion<interface::GoalSpecs, Spec>;

  /**
    Spec is any spec in ProblemSolverThrift.ConstraintSpecs, except the
    following:
    + AvoidMovingSpec
    + MovesInProgressSpec
    + MoveGroupSpec

    For the three specs above, use the API defined below
  */
  template <typename Spec>
  ProblemSolver& addConstraint(
      Spec spec,
      std::optional<ConstraintPolicy> policy = std::nullopt,
      std::optional<double> invalidCost = std::nullopt,
      std::optional<double> invalidState = std::nullopt,
      std::optional<int> tuplePosIfBroken = std::nullopt)
    requires(
        FieldTypeExistsInThriftStructOrUnion<
            interface::ConstraintSpecs,
            Spec> &&
        !isSingleParameterConstraint<Spec>);

  template <typename Spec>
  ProblemSolver& addConstraint(Spec spec)
    requires(isSingleParameterConstraint<Spec>);

  // Append a solver stage. Multiple addSolver calls run sequentially, each
  // starting from the previous stage's solution.
  ProblemSolver& addSolver(const LocalSearchSolverSpec& spec);
  ProblemSolver& addSolver(const LocalSearchStageSolverSpec& spec);
  ProblemSolver& addSolver(const OptimalSolverSpec& spec);
  ProblemSolver& addSolver(const OptimalSubsetSolverSpec& spec);
  ProblemSolver& addSolver(const RasHybridSolverSpec& spec);

  // Build and solve the configured problem; returns the resulting
  // AssignmentSolution. Idempotent: re-solving without further setup
  // returns the cached solution.
  AssignmentSolution solve();

  // Skip the post-solve summary computation (useful when the summary is
  // large and the caller doesn't need it).
  void disableSolutionSummary();

  // Suppress logging of run metadata to the rebalancer_runs Scuba table.
  ProblemSolver& disableLogging();

  // Return the unique run identifier assigned to this solve.
  std::string getRunId() const;

  // Optionally pass an AsyncManifoldUploadHandle to make uploads async.
  // The caller must call handle->wait() to wait for uploads to complete.
  // Otherwise, it waits in the destructor.
  void persistToManifold(
      std::shared_ptr<AsyncManifoldUploadHandle> manifoldUploadHandle =
          nullptr);

  // Set the XLOG verbosity level (e.g. "INFO", "DBG2"). Affects all
  // ProblemSolver instances in the process.
  static void setLogLevel(const std::string& level);

  // Opt into the parallelized new materializer (a performance experiment).
  // Default is the non-parallelized path.
  void useParallelizedNewMaterializer();

  // if set to true, this uses the new way of iterating over objects in
  // rebalancer that is faster when using local search and avoids potentially
  // exploring the same moves several times; this is no-op for optimal solver
  void shouldUseDynamicObjectOrdering(bool useDynamicObjectOrdering);

  // Enable the invalid move filter which pre-computes disallowed (Object,
  // Container) pairs and skips them upfront. This improves latency when
  // constraint violations invalidate many evaluations, but consumes extra
  // memory proportional to the number of invalid (object, container) pairs.
  void enableInvalidMoveFilter(bool enable);

  // Scope used by the optimal solver to partition the problem into
  // independently-solvable subproblems (decomposition).
  void setDecompositionScope(const std::string& scopeName);

  // publish metrics like utilization of a container (and other values of
  // interest) to Assignment.initialMetrics() and
  // AssignmentSolution.finalMetrics()
  void publishMetrics();

  // Attach equivalence-set diagnostics (which objects/containers were
  // grouped) to the returned solution.
  void publishEquivalenceSetInfo();

  // Construct a MoveTypeSpec union by tagging the given concrete spec
  // with the matching union arm. T must be one of the field types in
  // MoveTypeSpec.
  template <typename T>
  static MoveTypeSpec makeMoveTypeSpec(T moveTypeSpec) {
    return algopt::utils::createThriftUnionByField<MoveTypeSpec, T>(
        std::move(moveTypeSpec));
  }

  // deprecated APIs; DO NOT USE
  // TODO: remove BalanceV2 once Torch models stop using it.
  ProblemSolver& addGoal(
      const BalanceV2Spec& spec,
      double weight = 1,
      std::optional<int> tuplePos = std::nullopt);

  // prints a summary of the problem setup namely scopes, dimensions, goals etc
  // should only be called once all the goals and constraints are added or else
  // the summary will be incomplete
  void printProblemSetup() const;

  // returns the problem setup summary as a string
  std::string getProblemSummary() const;

  // Set absolute and relative epsilons used by internal floating-point
  // comparisons. The absolute epsilon must exceed
  // std::numeric_limits<double>::epsilon().
  void setPrecision(
      algopt::common::thrift::PrecisionTolerances precisionTolerance);

  // Snapshot the configured problem and (optionally) the solution into a
  // Bundle suitable for serialization or replay.
  Bundle getBundle() const;

  // Serialize the bundle (problem + optional solution) to `path` using the same
  // format as Manifold (zstd-compressed Thrift Binary), loadable by the
  // standalone Explorer. Needs no Manifold. Call after solve(). Throws on write
  // failure.
  void saveBundle(const std::string& path) const;

  // Print the command that re-runs the given run from its Manifold
  // snapshot via the standalone solver. Used by the background uploader.
  static void printReRunManifoldCommand(const std::string& runId);

  // Build a CPU thread-pool executor suitable for passing to the
  // ProblemSolver constructor.
  static std::shared_ptr<folly::ThreadPoolExecutor> makeCPUThreadPoolExecutor(
      std::string_view threadPoolNamePrefix,
      size_t numThreads);

 private:
  UniverseProblemBuilder& getProblemBuilder();
  StrategyBuilder& getStrategyBuilder();
  ConstraintPolicy getConstraintPolicy() const;
  ConstraintParams getDefaultConstraintParams() const;
  MoveStatsSpec getMoveStatsSpec() const;
  MoveValidatorSpec getMoveValidatorSpec() const;
  bool isMoveValidatorEnabled() const;
  bool isProfilerEnabled() const;
  bool isLoggingEnabled() const;
  std::string getService() const;
  std::string getScope() const;
  void enableMoveStatsImpl(MoveStatsSpec spec);

  void logProblemSetupTime() const;

  template <typename ScopeItemToObjectToValue>
  ProblemSolver& addDynamicObjectDimensionInner(
      const std::string& dimensionName,
      const std::string& scopeName,
      ScopeItemToObjectToValue scopeItemToObjectToValue,
      double defaultValue)
    requires IsMapOfMap<
        ScopeItemToObjectToValue,
        std::string,
        std::string,
        double>;

  template <typename ObjectToGroup>
  ProblemSolver& addPartitionInner(
      const std::string& partitionName,
      ObjectToGroup objectToGroup)
    requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>;

  template <typename GroupToObjects>
  ProblemSolver& addPartitionInner(
      const std::string& partitionName,
      GroupToObjects groupToObjects)
    requires IsIterableOverPairs<
        GroupToObjects,
        std::string,
        std::vector<std::string>>;

  template <typename ObjectToValue, typename ScaleByUsageMap>
  ProblemSolver& addObjectDimensionInner(
      const std::string& dimensionName,
      ObjectToValue objectToValue,
      double defaultValue,
      std::optional<ScaleByUsageMap> scaleByUsageMap)
    requires IsIterableOverPairs<ObjectToValue, std::string, double> &&
      IsIterableOverPairs<ScaleByUsageMap, std::string, double>;

  template <typename ObjectToValues>
  ProblemSolver& addObjectDimensionInner(
      const std::string& dimensionName,
      ObjectToValues objectToValues,
      double defaultValue)
    requires IsIterableOverPairs<
        ObjectToValues,
        std::string,
        std::vector<double>>;

  template <typename ContainerToObjects>
  ProblemSolver& setExpandedAssignment(
      const ContainerToObjects& containerToObjects)
    requires IsIterableOverPairs<
        ContainerToObjects,
        std::string,
        std::vector<std::string>>;

  ProblemSolver& setCompactAssignmentInner(
      const ContainerAssignment& compactAssignment);

  void persistToManifoldImpl(AsyncManifoldUploadHandle& manifoldUploadHandle);

  template <typename Spec>
  Spec expandSpecForCompactAssignment(Spec spec) const;

 private:
  std::shared_ptr<folly::ThreadPoolExecutor> executor;

  ProblemChecker checker;
  UniverseProblemBuilder universeProblemBuilder_;
  ThriftStrategyBuilder strategyBuilder;

  std::optional<AssignmentProblem> problem;
  std::optional<AssignmentSolution> solution;
  std::shared_ptr<const entities::Universe> universe_ = nullptr;
  std::shared_ptr<RebalancerLog> logger_ = nullptr;

  std::string runId;
  MoveStatsSpec moveStatsSpec = {};
  MoveValidatorSpec moveValidatorSpec = {};
  bool moveValidatorEnabled = false;
  bool profilerEnabled = false;
  bool loggingEnabled = true;
  bool getSolutionSummary = true;
  std::string service;
  std::string scope;
  ConstraintPolicy constraintPolicy = ConstraintPolicy::DEFAULT;
  ConstraintParams constraintParams;
  std::vector<std::string> descendingHotnessContainersOverride;
  int currentGoalTupleIndex_ = 0;

  // when set to true, it will just prepare the local problem and skip the
  // solving
  bool prepareProblemOnly = false;
  ManifoldBackupParams manifoldBackupParams;

  // default to using the non-parallelized version
  bool useParallelizedNewMaterializer_ = false;

  bool useDynamicObjectOrdering_ = true;

  // When enabled, pre-compute invalid (object, container) pairs and skip them
  // during move evaluation to improve performance.
  bool enableInvalidMoveFilter_ = false;

  // if specified, optimal slover will use this scope to partition the problem
  // into subproblems
  std::optional<std::string> decompositionScopeName_;
  // if specified, this label will be used to identify the rebalancer run
  // in logging output
  std::optional<std::string> loggingLabel_;

  bool publishMetrics_ = false;
  bool publishEquivalenceSetInfo_ = false;

  std::shared_ptr<AsyncManifoldUploadHandle> manifoldUploadHandle_{nullptr};

  algopt::Timer problemSetupTime_;
  std::optional<ObjectCountExpander> objectCountExpander_;
};

// implementations

template <typename ContainerToValue>
ProblemSolver& ProblemSolver::addContainerDimension(
    const std::string& dimensionName,
    const ContainerToValue& containerToValue,
    double defaultValue)
  requires IsIterableOverPairs<ContainerToValue, std::string, double>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addContainerDimension(dimensionName, containerToValue);
  getProblemBuilder().addContainerDimension(
      dimensionName, containerToValue, defaultValue);
  return *this;
}

template <typename ScopeItemToObjectToValue>
ProblemSolver& ProblemSolver::addDynamicObjectDimensionInner(
    const std::string& dimensionName,
    const std::string& scopeName,
    ScopeItemToObjectToValue scopeItemToObjectToValue,
    double defaultValue)
  requires IsMapOfMap<
      ScopeItemToObjectToValue,
      std::string,
      std::string,
      double>
{
  checker.addDynamicObjectDimension(
      dimensionName, scopeName, scopeItemToObjectToValue);
  getProblemBuilder().addDynamicObjectDimension(
      dimensionName,
      scopeName,
      std::move(scopeItemToObjectToValue),
      defaultValue);
  return *this;
}

template <typename ObjectToGroup>
ProblemSolver& ProblemSolver::addPartitionInner(
    const std::string& partitionName,
    ObjectToGroup objectToGroup)
  requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>
{
  checker.addPartition(partitionName, objectToGroup);
  getProblemBuilder().addPartition(partitionName, std::move(objectToGroup));
  return *this;
}

template <typename GroupToObjects>
ProblemSolver& ProblemSolver::addPartitionInner(
    const std::string& partitionName,
    GroupToObjects groupToObjects)
  requires IsIterableOverPairs<
      GroupToObjects,
      std::string,
      std::vector<std::string>>
{
  checker.addPartition(partitionName, groupToObjects);
  getProblemBuilder().addPartition(partitionName, std::move(groupToObjects));
  return *this;
}

template <typename ObjectToValue, typename ScaleByUsageMap>
ProblemSolver& ProblemSolver::addObjectDimensionInner(
    const std::string& dimensionName,
    ObjectToValue objectToValue,
    double defaultValue,
    std::optional<ScaleByUsageMap> scaleByUsageMap)
  requires IsIterableOverPairs<ObjectToValue, std::string, double> &&
    IsIterableOverPairs<ScaleByUsageMap, std::string, double>
{
  checker.addObjectDimension(dimensionName, objectToValue);
  getProblemBuilder().addObjectDimension(
      dimensionName,
      std::move(objectToValue),
      defaultValue,
      std::move(scaleByUsageMap));
  return *this;
}

template <typename ObjectToValues>
ProblemSolver& ProblemSolver::addObjectDimensionInner(
    const std::string& dimensionName,
    ObjectToValues objectToValues,
    double defaultValue)
  requires IsIterableOverPairs<ObjectToValues, std::string, std::vector<double>>
{
  checker.addObjectDimension(dimensionName, objectToValues);
  getProblemBuilder().addObjectDimension(
      dimensionName, std::move(objectToValues), defaultValue);
  return *this;
}

template <typename ScopeItemToObjectToValue>
ProblemSolver& ProblemSolver::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    ScopeItemToObjectToValue scopeItemToObjectToValue,
    double defaultValue)
  requires IsMapOfMap<
      ScopeItemToObjectToValue,
      std::string,
      std::string,
      double>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  if (!objectCountExpander_) {
    return addDynamicObjectDimensionInner(
        dimensionName,
        scopeName,
        std::move(scopeItemToObjectToValue),
        defaultValue);
  }

  folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
      expanded;
  for (const auto& [scopeItem, objectToValue] : scopeItemToObjectToValue) {
    expanded[scopeItem] = objectCountExpander_->expandObjectMap(objectToValue);
  }
  return addDynamicObjectDimensionInner(
      dimensionName, scopeName, std::move(expanded), defaultValue);
}

template <typename ScopeItemToGroupToValue>
ProblemSolver& ProblemSolver::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    const std::string& partitionName,
    ScopeItemToGroupToValue scopeItemToGroupToValue,
    double defaultValue)
  requires IsMapOfMap<ScopeItemToGroupToValue, std::string, std::string, double>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  // Partition-based dynamic dimensions operate on group names, not object
  // names, so no expansion is needed regardless of objectCountExpander_.
  checker.addDynamicObjectDimension(
      dimensionName, scopeName, partitionName, scopeItemToGroupToValue);
  getProblemBuilder().addDynamicObjectDimension(
      dimensionName,
      scopeName,
      partitionName,
      std::move(scopeItemToGroupToValue),
      defaultValue);

  return *this;
}

template <typename ObjectToGroup>
ProblemSolver& ProblemSolver::addPartition(
    const std::string& partitionName,
    ObjectToGroup objectToGroup)
  requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  if (!objectCountExpander_) {
    return addPartitionInner(partitionName, std::move(objectToGroup));
  }

  auto expandedObjectToGroup =
      objectCountExpander_->expandObjectMap(objectToGroup);
  return addPartitionInner(partitionName, std::move(expandedObjectToGroup));
}

template <typename GroupToObjects>
ProblemSolver& ProblemSolver::addPartition(
    const std::string& partitionName,
    GroupToObjects groupToObjects)
  requires IsIterableOverPairs<
      GroupToObjects,
      std::string,
      std::vector<std::string>>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  if (!objectCountExpander_) {
    // No compact-assignment expansion is active, so pass through unchanged.
    // Converting to
    // object->group would lose data when objects belong to multiple groups.
    return addPartitionInner(partitionName, std::move(groupToObjects));
  }

  // Expand group->objects directly: replace each original object name
  // with its synthetic names, preserving the many-to-many relationship.
  auto expandedGroupToObjects = objectCountExpander_->expandGroupToObjects(
      groupToObjects, "addPartition");
  return addPartitionInner(partitionName, std::move(expandedGroupToObjects));
}

template <typename ScopeItemToValue>
ProblemSolver& ProblemSolver::addScopeDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    const ScopeItemToValue& scopeItemToValue,
    double defaultValue)
  requires IsIterableOverPairs<ScopeItemToValue, std::string, double>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addScopeDimension(dimensionName, scopeName, scopeItemToValue);
  getProblemBuilder().addScopeDimension(
      dimensionName, scopeName, scopeItemToValue, defaultValue);
  return *this;
}

template <typename ContainerToScopeItem>
ProblemSolver& ProblemSolver::addScope(
    const std::string& scopeName,
    const ContainerToScopeItem& containerToScopeItem)
  requires IsIterableOverPairs<ContainerToScopeItem, std::string, std::string>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addScope(scopeName);
  for (auto& containerScopeItem : containerToScopeItem) {
    checker.checkContainerExists(containerScopeItem.first);
    checker.addScopeItem(scopeName, containerScopeItem.second);
  }
  getProblemBuilder().addScope(scopeName, containerToScopeItem);
  return *this;
}

template <typename ScopeItemToContainers>
ProblemSolver& ProblemSolver::addScope(
    const std::string& scopeName,
    const ScopeItemToContainers& scopeItemToContainers)
  requires IsIterableOverPairs<
      ScopeItemToContainers,
      std::string,
      std::vector<std::string>>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addScope(scopeName);
  for (const auto& [scopeItem, containers] : scopeItemToContainers) {
    checker.addScopeItem(scopeName, scopeItem);
    for (const auto& container : containers) {
      // It checks for both container existence and duplicates within the scope.
      // Duplicate container checking is only needed for the
      // scopeItem->containers version of addScope. The container->scopeItem
      // version of addScope does not need to check for container duplicates as
      // they are impossible to define via the API (keys of a map are
      // necessarily unique).
      checker.addScopeContainer(scopeName, container);
    }
  }
  getProblemBuilder().addScope(scopeName, scopeItemToContainers);
  return *this;
}

template <typename ContainerToObjects>
ProblemSolver& ProblemSolver::setAssignment(
    const ContainerToObjects& containerToObjects)
  requires IsIterableOverPairs<
      ContainerToObjects,
      std::string,
      std::vector<std::string>>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  return setExpandedAssignment(containerToObjects);
}

template <typename ContainerToObjects>
ProblemSolver& ProblemSolver::setExpandedAssignment(
    const ContainerToObjects& containerToObjects)
  requires IsIterableOverPairs<
      ContainerToObjects,
      std::string,
      std::vector<std::string>>
{
  checker.resetAssignment();
  for (auto& [containerName, objectNames] : containerToObjects) {
    checker.addContainer(containerName);
    for (auto& objectName : objectNames) {
      checker.addObject(objectName);
    }
  }

  getProblemBuilder().setAssignment(containerToObjects);
  return *this;
}

template <typename CompactAssignmentInput>
ProblemSolver& ProblemSolver::setAssignment(
    const CompactAssignmentInput& compactAssignmentInput)
  requires IsCompactAssignmentInput<CompactAssignmentInput>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();

  ContainerAssignment compactAssignment;
  auto& objectsPerContainer = *compactAssignment.objectsPerContainer();
  for (const auto& [container, objectCounts] : compactAssignmentInput) {
    auto& containerObjectCounts = objectsPerContainer[container];
    for (const auto& [objectName, count] : objectCounts) {
      containerObjectCounts[objectName] = count;
    }
  }

  return setCompactAssignmentInner(compactAssignment);
}

template <typename ObjectToValue, typename ScaleByUsageMap>
ProblemSolver& ProblemSolver::addObjectDimension(
    const std::string& dimensionName,
    ObjectToValue objectToValue,
    double defaultValue,
    std::optional<ScaleByUsageMap> scaleByUsageMap)
  requires IsIterableOverPairs<ObjectToValue, std::string, double> &&
    IsIterableOverPairs<ScaleByUsageMap, std::string, double>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  if (!objectCountExpander_) {
    return addObjectDimensionInner(
        dimensionName,
        std::move(objectToValue),
        defaultValue,
        std::move(scaleByUsageMap));
  }

  auto expandedObjectToValue =
      objectCountExpander_->expandObjectMap(objectToValue);
  return addObjectDimensionInner(
      dimensionName,
      std::move(expandedObjectToValue),
      defaultValue,
      std::move(scaleByUsageMap));
}

template <typename ObjectToValues>
ProblemSolver& ProblemSolver::addObjectDimension(
    const std::string& dimensionName,
    ObjectToValues objectToValues,
    double defaultValue)
  requires IsIterableOverPairs<ObjectToValues, std::string, std::vector<double>>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  if (!objectCountExpander_) {
    return addObjectDimensionInner(
        dimensionName, std::move(objectToValues), defaultValue);
  }

  auto expandedObjectToValues =
      objectCountExpander_->expandObjectMap(objectToValues);
  return addObjectDimensionInner(
      dimensionName, std::move(expandedObjectToValues), defaultValue);
}

template <typename Spec>
ProblemSolver&
ProblemSolver::addGoal(Spec spec, double weight, std::optional<int> tuplePos)
  requires FieldTypeExistsInThriftStructOrUnion<interface::GoalSpecs, Spec>
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkTuplePos(tuplePos);
  spec = expandSpecForCompactAssignment(std::move(spec));

  if constexpr (std::same_as<Spec, ToFreeSpec>) {
    checker.addSpec(spec, false /*isConstraint*/);
  } else {
    checker.addSpec(spec);
  }

  const auto tupleIndex = tuplePos.value_or(currentGoalTupleIndex_);
  getProblemBuilder().addGoal(std::move(spec), weight, tupleIndex);
  return *this;
}

template <typename Spec>
ProblemSolver& ProblemSolver::addConstraint(
    Spec spec,
    std::optional<ConstraintPolicy> policy,
    std::optional<double> invalidCost,
    std::optional<double> invalidState,
    std::optional<int> tuplePosIfBroken)
  requires(
      FieldTypeExistsInThriftStructOrUnion<interface::ConstraintSpecs, Spec> &&
      !isSingleParameterConstraint<Spec>)
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkTuplePos(tuplePosIfBroken);
  spec = expandSpecForCompactAssignment(std::move(spec));

  if constexpr (std::same_as<Spec, ToFreeSpec>) {
    checker.addSpec(spec, true /*isConstraint*/);
  } else {
    checker.addSpec(spec);
  }

  getProblemBuilder().addConstraint(
      std::move(spec), policy, invalidCost, invalidState, tuplePosIfBroken);
  return *this;
}

template <typename Spec>
ProblemSolver& ProblemSolver::addConstraint(Spec spec)
  requires(isSingleParameterConstraint<Spec>)
{
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  spec = expandSpecForCompactAssignment(std::move(spec));
  checker.addSpec(spec);
  getProblemBuilder().addConstraint(
      std::move(spec), std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  return *this;
}

template <typename Spec>
Spec ProblemSolver::expandSpecForCompactAssignment(Spec spec) const {
  if (objectCountExpander_) {
    return objectCountExpander_->maybeExpandSpec(std::move(spec));
  }
  return spec;
}

} // namespace facebook::rebalancer::interface
