// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <folly/coro/AsyncScope.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/SharedPromise.h>
#include <folly/Synchronized.h>

#include <string>
#include <vector>

namespace facebook::rebalancer::explorer {

struct EquivalenceSetsData {
  std::string partitionName;
  std::vector<std::string> groupNames;
  entities::Map<entities::ObjectId, std::string> objectIdToGroupName;
};

struct DynamicDimensionColumns {
  explicit DynamicDimensionColumns(
      const entities::Universe& universe,
      const entities::ObjectScalarDimension& dimension,
      const std::string& dimensionName,
      const entities::Map<
          entities::ContainerId,
          std::vector<entities::ObjectId>>& finalAssignment);
  // Columns for the object table. Contains
  // values of the dimension for each
  // object based on its initial and final assignment.
  std::shared_ptr<Column> srcColumn; // w.r.t. initial assignment
  std::shared_ptr<Column> dstColumn; // w.r.t. final assignment

  // Columns for dynamic dimension table. Object-keyed dimensions contain one
  // row for each {objectId, scopeItemId}; group-keyed dimensions contain one
  // row for each {groupId, scopeItemId}.
  std::shared_ptr<Column> objectNamesColumn;
  std::shared_ptr<Column> scopeItemNamesColumn;
  std::shared_ptr<Column> dimensionValuesColumn;

  // contains a logical EntityId for each {objectId, scopeItemId} pair to
  // identify the corresponding value of the dynamic dimension.
  std::vector<EntityId> rowIds;
};
struct ExplorerModel {
  interface::AssignmentProblem problemSpec;
  std::shared_ptr<const entities::Universe> universe;
  entities::Map<std::string, Table> tableData;
  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      finalAssignment;
  std::optional<interface::AssignmentSolution> solution;
  std::shared_ptr<const MaterializedProblem> materialized;
  std::vector<std::string> dynamicDimensionNames;
  std::unique_ptr<Problem> problem;
  EquivalenceSetsData equivalenceSetsData;
};

class LoadModel {
  /* Stores data about object, container and scope items. */
 private:
  /* Ensure object cannot be created for this class. */
  explicit LoadModel();

 public:
  static ExplorerModel buildData(interface::Bundle&& bundle);
  static void initDynamicDimensionTables(
      const entities::Universe& universe,
      const entities::Map<
          entities::ContainerId,
          std::vector<entities::ObjectId>>& finalAssignment,
      entities::Map<std::string, std::shared_ptr<folly::SharedPromise<Table>>>&
          tablePromises,
      folly::coro::AsyncScope& asyncScope,
      folly::Executor* executor);

  // Builds and inserts dynamic dimension columns (srcColumn, dstColumn)
  // into the objects table asynchronously. This is fully independent of
  // initDynamicDimensionTables and computes the columns from scratch.
  static folly::coro::Task<void> initDynamicObjectDimensionColsAsync(
      const entities::Universe& universe,
      const entities::Map<
          entities::ContainerId,
          std::vector<entities::ObjectId>>& finalAssignment,
      entities::Map<std::string, Table>& tableData,
      folly::coro::AsyncScope& asyncScope,
      std::shared_ptr<folly::CPUThreadPoolExecutor>& executor);

  static void buildObjectDimensionCols(
      const entities::Universe& universe,
      const entities::Map<std::string, DynamicDimensionColumns>&
          dynamicDimensions,
      entities::Map<std::string, Table>& tableData);

  static void buildStaticObjectDimensionCols(
      const entities::Universe& universe,
      entities::Map<std::string, Table>& tableData);

  static void buildDynamicObjectDimensionCols(
      const entities::Universe& universe,
      entities::Map<std::string, Table>& tableData);

  static std::vector<std::string> getDynamicDimensionNames(
      const entities::Universe& universe);

  static std::vector<std::shared_ptr<const Column>> buildPartitionCols(
      const entities::Universe& universe,
      const EquivalenceSetsData& eqSetsData);

  static std::vector<std::shared_ptr<const Column>> buildAssignmentCols(
      const entities::Universe& universe,
      const entities::Map<
          entities::ContainerId,
          std::vector<entities::ObjectId>>& finalAssignment);
};

} // namespace facebook::rebalancer::explorer
