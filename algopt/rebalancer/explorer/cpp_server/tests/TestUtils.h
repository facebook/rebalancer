// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once
#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <memory>
#include <optional>

namespace facebook {
namespace rebalancer {
namespace explorer {

struct BuildFilesOptions {
  std::optional<interface::AvoidMovingSpec> spec = std::nullopt;
  std::optional<interface::MovesInProgressSpec> inProgressSpec = std::nullopt;
  bool includeMovesSummary = false;
  bool includeOverlappedPartition = false;
  bool includeSolutionObject = true;
  bool useLocalSearchStageSolver = false;
};

struct BuildWithConstraintsAndGoalsConfig {
  std::optional<interface::MoveStatsSpec> moveStatsSpec = std::nullopt;
};

struct BuildWithPenaltyOnlyMoveConfig {
  // std::nullopt leaves the field unset, like a run saved before it existed.
  std::optional<bool> useSeparatedLocalSearchPenaltyObjective = true;
  // Record objective changes in MovesSummary instead of letting the explorer
  // replay the moves.
  bool recordObjectiveChangesInMovesSummary = false;
};

class TestUtils {
 private:
  explicit TestUtils();

 public:
  static std::unique_ptr<ModelServer> buildUniverse(
      std::optional<interface::AvoidMovingSpec> spec = std::nullopt,
      std::optional<interface::MovesInProgressSpec> inProgressSpec =
          std::nullopt,
      bool includeMovesSummary = false,
      bool includeOverlappedPartition = false,
      bool useLocalSearchStageSolver = false);
  static std::unique_ptr<ModelServer> buildUniverseWithConstraintsAndGoals(
      BuildWithConstraintsAndGoalsConfig config);
  // Three tasks of two jobs start on host0 under a GroupDiversitySpec goal
  // allowing one job per host. MoveSet 1 keeps both jobs on host0, so it only
  // changes the separated penalty; MoveSet 2 leaves one job per host.
  static std::unique_ptr<ModelServer> buildUniverseWithPenaltyOnlyMove(
      BuildWithPenaltyOnlyMoveConfig config);

  static interface::Bundle buildBundle(
      const BuildFilesOptions& buildOptions = BuildFilesOptions());
  static interface::Bundle buildConstraintsAndGoalsBundle(
      BuildWithConstraintsAndGoalsConfig config);
  static interface::Bundle buildEvaluatorProblem();
  static Query prepareQuery(
      const std::string& tableName,
      const std::vector<FilterRule>& rules);
  static Query prepareQuery(
      const std::string& tableName,
      const Group& group,
      std::optional<std::vector<FilterRule>> rules = std::nullopt);
  static Query prepareQuery(
      const std::string& tableName,
      const Order& Order,
      std::optional<Page> page = std::nullopt);
  static TypeaheadRequest prepareTypeheadRequest(
      const std::string& entity,
      const std::string& query,
      const int limit);
  static Assignment prepareAssigmentRequest(
      std::map<std::string, std::string> variableToContainerOverride,
      AssignmentBase base = AssignmentBase::INITIAL);
  static MovesBetweenAssignmentsRequest prepareMoveBetweenAssigmentRequest(
      Assignment source,
      Assignment destination);
  static void writeToFile(
      const std::string& filename,
      const std::string& content);
  static MoveSetsRequest prepareMoveSetsRequest(
      int64_t startMoveSetIdx,
      int64_t endMoveSetIdx,
      std::optional<std::string> partitionName = std::nullopt,
      std::optional<std::string> scopeName = std::nullopt);

  struct Move {
    std::string object;
    std::string srcContainer;
    std::string dstContainer;
  };
  struct MoveSet {
    std::vector<Move> moves;
    folly::F14FastMap<std::string, double> objectiveNameToChange;
  };
  static std::vector<interface::MovesSummary> makeMovesSummaries(
      const std::vector<MoveSet>& moveSets);
};

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
