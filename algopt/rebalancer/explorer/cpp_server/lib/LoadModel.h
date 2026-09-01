// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "rebalancer/explorer/cpp_server/lib/TableStore.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

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
  TableStore tableStore;
  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      finalAssignment;
  std::optional<interface::AssignmentSolution> solution;
  std::shared_ptr<const MaterializedProblem> materialized;
  std::vector<std::string> dynamicDimensionNames;
  std::unique_ptr<Problem> problem;
  std::shared_ptr<const EquivalenceSetsData> equivalenceSetsData;
  std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor;
};

class LoadModel {
  /* Stores data about object, container and scope items. */
 private:
  /* Ensure object cannot be created for this class. */
  explicit LoadModel();

 public:
  static ExplorerModel buildData(interface::Bundle&& bundle);
};

} // namespace facebook::rebalancer::explorer
