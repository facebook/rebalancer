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
  static folly::coro::Task<Table> buildObjectTable(
      const entities::Universe& universe,
      const entities::Map<
          entities::ContainerId,
          std::vector<entities::ObjectId>>& containerIdToFinalObjectIds,
      const EquivalenceSetsData& equivalenceSetsData,
      folly::Executor* executor);
  static Table buildDynamicDimensionTable(
      const entities::Universe& universe,
      const entities::ObjectScalarDimension& dimension,
      const std::string& dimensionName);
  static void initDynamicDimensionTables(
      const entities::Universe& universe,
      entities::Map<std::string, std::shared_ptr<folly::SharedPromise<Table>>>&
          tablePromises,
      folly::coro::AsyncScope& asyncScope,
      folly::Executor* executor);
  static std::vector<std::string> getDynamicDimensionNames(
      const entities::Universe& universe);
};

} // namespace facebook::rebalancer::explorer
