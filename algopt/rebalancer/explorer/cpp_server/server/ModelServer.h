// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "rebalancer/explorer/cpp_server/lib/LoadModel.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/coro/AsyncScope.h>
#include <folly/coro/Task.h>
#include <folly/futures/Future.h>
#include <folly/futures/SharedPromise.h>
#include <folly/hash/Hash.h>
#include <folly/synchronization/CallOnce.h>
#include <folly/Synchronized.h>

#include <string>

namespace {
std::size_t hashAssignment(
    const facebook::rebalancer::explorer::Assignment& assignment) {
  const auto h1 = std::hash<facebook::rebalancer::explorer::AssignmentBase>{}(
      *assignment.base());

  // Hash variableToContainerOverride map
  std::size_t h2 = 0;
  for (const auto& [key, value] : *assignment.variableToContainerOverride()) {
    const auto keyHash = std::hash<std::string>{}(key);
    const auto valueHash = std::hash<std::string>{}(value);
    h2 = folly::hash::hash_combine(h2, keyHash, valueHash);
  }

  // Hash the optional searchStep
  const auto h3 = assignment.searchStep().has_value()
      ? std::hash<int64_t>{}(*assignment.searchStep())
      : 0;

  return folly::hash::hash_combine(h1, h2, h3);
}
} // namespace

namespace std {
template <>
struct hash<std::tuple<
    facebook::rebalancer::interface::thrift::MetricCollectionType,
    facebook::rebalancer::explorer::Assignment,
    facebook::rebalancer::explorer::Assignment>> {
  std::size_t operator()(
      const std::tuple<
          facebook::rebalancer::interface::thrift::MetricCollectionType,
          facebook::rebalancer::explorer::Assignment,
          facebook::rebalancer::explorer::Assignment>& t) const {
    const auto& [metricType, assignment1, assignment2] = t;
    return folly::hash::hash_combine(
        std::hash<
            facebook::rebalancer::interface::thrift::MetricCollectionType>{}(
            metricType),
        hashAssignment(assignment1),
        hashAssignment(assignment2));
  }
};

template <>
struct hash<facebook::rebalancer::explorer::ExportTableRequest> {
  std::size_t operator()(
      const facebook::rebalancer::explorer::ExportTableRequest& request) const {
    const auto h1 = std::hash<std::string>{}(*request.tableName());
    const auto h2 = request.assignmentA().has_value()
        ? hashAssignment(*request.assignmentA())
        : 0;
    const auto h3 = request.assignmentB().has_value()
        ? hashAssignment(*request.assignmentB())
        : 0;

    return folly::hash::hash_combine(h1, h2, h3);
  }
};
} // namespace std

namespace facebook::rebalancer::explorer {

using TablePromises =
    entities::Map<std::string, std::shared_ptr<folly::SharedPromise<Table>>>;

class ModelServer {
  /* Creates model and performs operation on them. */

 public:
  explicit ModelServer(interface::Bundle&& bundle);
  ~ModelServer();

  // query model
  folly::coro::Task<Result> getData(const Query& query) const;

  folly::coro::Task<TypeaheadResponse> getTypeahead(
      const TypeaheadRequest& request) const;

  ProblemMetadata getProblemMetadata() const;

  MovesBetweenAssignmentsResponse getMovesBetweenAssignments(
      const MovesBetweenAssignmentsRequest& request) const;

  folly::coro::Task<MetricDistributionResponse> getMetricDistribution(
      const MetricDistributionRequest& request) const;

  std::vector<interface::LocalSearchProfile> getLocalSearchProfiles() const;

  MoveSetsResponse getMoveSets(const MoveSetsRequest& request) const;

  EvaluationResult evaluate(const Assignment& assignment) const;

  Result evaluateMetricCollection(
      const Query& query,
      const Assignment& assignmentA,
      const Assignment& assignmentB) const;

  GoalSpec getGoalSpec(const std::string& name) const;
  ConstraintSpec getConstraintSpec(const std::string& name) const;

  TreeNodeResponse getTreeNode(const TreeNodeRequest& request) const;

  EditProblemResponse editProblem(const EditProblemRequest& request) const;

  folly::coro::Task<ExportTableResponse> exportTable(
      const ExportTableRequest& request) const;

  int getExpressionCount() const;

 private:
  rebalancer::Assignment getInitialAssignment() const;
  entities::Map<entities::ObjectId, entities::ContainerId> getObjectToContainer(
      const Assignment& assignment) const;
  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
  buildIntermediateAssignment(const Assignment& assignment) const;
  ChangeSet getChangesFromInitial(const Assignment& assignment) const;
  void initExpressionIdToPtr();
  void initExpressionIdToPtr(Expression* expression);
  void initObjectiveNameToExpr();
  void startTableDataAsync(entities::Map<std::string, Table> tableData);
  void waitForTableData() const;
  void initPropertiesIndex();
  entities::Set<int64_t> getReachableAncestors(
      const std::vector<Expression*>& startNodes) const;
  std::optional<folly::F14FastMap<std::string, std::vector<double>>>
  computeObjectiveToChangePerMoveSet() const;

  static Result processQueryOnTable(const Query& query, explorer::Table table);
  const Table& tablulateMetricCollection(
      interface::thrift::MetricCollectionType metricType,
      const Assignment& assignmentA,
      const Assignment& assignmentB) const;

  double getChangeInObjective(
      const std::string& objectiveName,
      const size_t moveSetIndex) const;
  std::string getGroupName(
      const std::string& objectName,
      const std::string& partitionName) const;
  std::string getScopeItemName(
      const std::string& containerName,
      const entities::Scope* scopePtr) const;

  ExpressionProperties replaceIdsWithNames(
      ExpressionProperties properties) const;
  ExpressionPropertyValue replaceIdsWithNames(
      ExpressionPropertyValue value) const;
  ExpressionPropertyValue replaceIdsWithNames(
      ExpressionPropertyValueContainerIdList value) const;
  ExpressionPropertyValue replaceIdsWithNames(
      ExpressionPropertyValueObjectIdDoubleMap value) const;
  ExpressionPropertyValue replaceIdsWithNames(
      ExpressionPropertyValueObjectId value) const;
  ExpressionPropertyValue replaceIdsWithNames(
      ExpressionPropertyValueContainerId value) const;

 private:
  interface::AssignmentProblem problemSpec_;
  std::shared_ptr<const entities::Universe> universe_;
  rebalancer::Assignment initialAssignment_;
  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      finalAssignment_;
  std::set<std::string> partitionNames_;
  EquivalenceSetsData equivalenceSetsData_;
  entities::Map<entities::ObjectId, entities::GroupId> partToMoveGroup_;
  std::optional<interface::AssignmentSolution> solution_;
  std::shared_ptr<const MaterializedProblem> materialized_;
  entities::Map<int64_t, Expression*> expressionIdToPtr_;
  std::vector<std::string> dynamicDimensionNames_;
  folly::F14FastMap<std::string, ExprPtr> objectiveNameToExpr_;
  entities::Map<std::string, interface::thrift::MetricCollectionType>
      metricCollectionNameToType_;
  std::unique_ptr<Problem> problem_;
  // `mutable` so `const` methods can populate them.
  // `materializer::Cache` is thread-safe.
  // `Table` is wrapped in `unique_ptr` because the cache requires
  // default-constructible values.
  mutable materializer::Cache<
      std::tuple<
          interface::thrift::MetricCollectionType,
          explorer::Assignment,
          explorer::Assignment>,
      std::unique_ptr<const explorer::Table>>
      metricCollectionTypeToFullTableCache_;
  mutable materializer::
      Cache<explorer::ExportTableRequest, explorer::ExportTableResponse>
          exportTableRequestToResponseCache_;
  rebalancer::SolverT solverSpec_;
  bool canDisplayObjChangesInMoveSetsTable_ = false;
  bool allObjChangesInMovesSummary_ = false;
  bool problemOnlyHasSingleMoves_ = true;
  std::optional<folly::F14FastMap<std::string, std::vector<double>>>
      objectiveToChangePerMoveSet_ = std::nullopt;

  folly::coro::AsyncScope asyncScope_;
  TablePromises tablePromises_;
  std::shared_ptr<folly::CPUThreadPoolExecutor> executor_;

  // entityIdToNodes_ mapping from entityId (container or object) to expressions
  // that have that entity in their properties (based on getProperties())
  // Initialized asynchronously in constructor, waited on first access
  mutable folly::SemiFuture<folly::Unit> propertiesIndexFuture_;
  mutable folly::once_flag propertiesIndexOnceFlag_;
  mutable entities::Map<explorer::EntityId, std::vector<Expression*>>
      entityIdToNodes_;

  void startPropertiesIndexAsync();
  void waitForPropertiesIndex() const;

  // Bundles metrics->fullApply() + initObjectiveNameToExpr() into a single
  // async task that runs them serially.
  void runMetricsAndObjectiveInit();
  void startMetricsAndObjectiveInitAsync();
  void waitForMetricsAndObjectiveInit() const;
  mutable folly::SemiFuture<folly::Unit> metricsAndObjectiveInitFuture_;
  mutable folly::once_flag metricsAndObjectiveInitOnceFlag_;

  mutable folly::SemiFuture<folly::Unit> tableDataFuture_;
  mutable folly::once_flag tableDataOnceFlag_;
};

} // namespace facebook::rebalancer::explorer
