// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/tests/utils.h"
#include "rebalancer/explorer/cpp_server/lib/LoadModel.h"
#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/cpp_server/tests/TestUtils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/interface/fb/Manifold.h"
#include "rebalancer/explorer/cpp_server/lib/fb/PrestoUtils.h"

#include "datainfra/presto/client_next/ligen/cpp_bindings/presto_next.h"
#endif

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Timeout.h>
#include <folly/Portability.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::explorer;
using namespace facebook::rebalancer::interface;

template <class T>
std::set<T> makeSet(std::initializer_list<T> items) {
  return std::set<T>(items);
}

template <class T>
std::set<T> makeSet(const std::vector<T>& items) {
  return std::set<T>(items.begin(), items.end());
}

class ModelServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    model = TestUtils::buildUniverse();
    modelWithMovesSummary = TestUtils::buildUniverse(
        std::nullopt, std::nullopt, /*includeMovesSummary=*/true);
    modelWithMovesSummaryAndStageSolver = TestUtils::buildUniverse(
        std::nullopt,
        std::nullopt,
        /*includeMovesSummary=*/true,
        /*includeOverlappedPartition=*/false,
        /*useLocalSearchStageSolver=*/true);
    modelWithOverlappedPartition = TestUtils::buildUniverse(
        std::nullopt,
        std::nullopt,
        /*includeMovesSummary=*/true,
        /*includeOverlappedPartition=*/true);
  }

  static std::unique_ptr<ModelServer> getModelWithConstraintsAndGoals(
      BuildWithConstraintsAndGoalsConfig config =
          BuildWithConstraintsAndGoalsConfig()) {
    return TestUtils::buildUniverseWithConstraintsAndGoals(std::move(config));
  }

  // Helper methods for synchronous access to async ModelServer methods
  static Result getData(ModelServer& server, const Query& query) {
    return folly::coro::blockingWait(server.getData(query));
  }

  static TypeaheadResponse getTypeahead(
      ModelServer& server,
      const TypeaheadRequest& request) {
    return folly::coro::blockingWait(server.getTypeahead(request));
  }

  static MetricDistributionResponse getMetricDistribution(
      ModelServer& server,
      const MetricDistributionRequest& request) {
    return folly::coro::blockingWait(server.getMetricDistribution(request));
  }

  static ExportTableResponse exportTable(
      ModelServer& server,
      const ExportTableRequest& request) {
    return folly::coro::blockingWait(server.exportTable(request));
  }

  std::unique_ptr<ModelServer> model;
  std::unique_ptr<ModelServer> modelWithMovesSummary;
  std::unique_ptr<ModelServer> modelWithMovesSummaryAndStageSolver;
  std::unique_ptr<ModelServer> modelWithOverlappedPartition;
};

TEST_F(ModelServerTest, ModelOrderStringAsc) {
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::ASCENDING;
  Order order;
  order.columns() = {oc};
  auto query = TestUtils::prepareQuery(std::string("host"), order);
  auto result = getData(*model, query);
  const std::vector<std::string> expectedOrder = {
      "host0", "host1", "host2", "host3", "host4"};
  std::vector<std::string> actualOrder;
  for (auto& row : *result.rows()) {
    actualOrder.push_back(*row.cells()->at(0).stringValue());
  }
  EXPECT_EQ(expectedOrder, actualOrder);
}

TEST_F(ModelServerTest, ModelOrderStringDesc) {
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::DESCENDING;
  Order order;
  order.columns() = {oc};
  auto query = TestUtils::prepareQuery(std::string("host"), order);
  auto result = getData(*model, query);
  const std::vector<std::string> expectedOrder = {
      "host4", "host3", "host2", "host1", "host0"};
  std::vector<std::string> actualOrder;
  for (auto& row : *result.rows()) {
    actualOrder.push_back(*row.cells()->at(0).stringValue());
  }
  EXPECT_EQ(expectedOrder, actualOrder);
}

TEST_F(ModelServerTest, ModelOrderNumericDsc) {
  OrderColumn oc;
  oc.name() = "ram";
  oc.direction() = OrderDirection::DESCENDING;
  Order order;
  order.columns() = {oc};
  auto query = TestUtils::prepareQuery(std::string("host"), order);
  auto result = getData(*model, query);

  EXPECT_EQ(result.rows()->size(), 5);
  // host1 has the smallest value, followed by host0; hosts 4, 3, and 2 have the
  // same value; so just check that the last value is "host1" and second last is
  // "host0"
  EXPECT_EQ(*result.rows()->at(4).cells()->at(0).stringValue(), "host1");
  EXPECT_EQ(*result.rows()->at(3).cells()->at(0).stringValue(), "host0");
}

TEST_F(ModelServerTest, ModelOrderNumericAsc) {
  OrderColumn oc;
  oc.name() = "host_count.initUtil";
  oc.direction() = OrderDirection::ASCENDING;
  Order order;
  order.columns() = {oc};
  auto query = TestUtils::prepareQuery(std::string("rack"), order);
  auto result = getData(*model, query);

  int msbIndex = 0;
  for (auto& column : *result.columns()) {
    if (*column.name() != "rack") {
      msbIndex++;
    } else {
      break;
    }
  }

  EXPECT_EQ(result.rows()->size(), 3);
  // "rack0" has the highest value; racks 2 and 1 have the same value for
  // host_count.initUtil; so just check that the last value is "rack0"
  EXPECT_EQ(*result.rows()->at(2).cells()->at(msbIndex).stringValue(), "rack0");
}

TEST_F(ModelServerTest, ModelPageBasic) {
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::DESCENDING;
  Order order;
  order.columns() = {oc};
  Page page;
  page.limit() = 2;
  page.offset() = 0;
  auto query = TestUtils::prepareQuery(std::string("host"), order, page);
  auto result = getData(*model, query);
  const std::vector<std::string> expectedOrder = {"host4", "host3"};
  std::vector<std::string> actualOrder;
  for (auto& row : *result.rows()) {
    actualOrder.push_back(*row.cells()->at(0).stringValue());
  }
  EXPECT_EQ(expectedOrder, actualOrder);
  EXPECT_EQ(5, *result.totalCount());
}

TEST_F(ModelServerTest, ModelPageOffset) {
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::DESCENDING;
  Order order;
  order.columns() = {oc};
  Page page;
  page.limit() = 2;
  page.offset() = 3;
  auto query = TestUtils::prepareQuery(std::string("host"), order, page);
  auto result = getData(*model, query);
  const std::vector<std::string> expectedOrder = {"host1", "host0"};
  std::vector<std::string> actualOrder;
  for (auto& row : *result.rows()) {
    actualOrder.push_back(*row.cells()->at(0).stringValue());
  }
  EXPECT_EQ(expectedOrder, actualOrder);
  EXPECT_EQ(5, *result.totalCount());
}

TEST_F(ModelServerTest, ModelPageHigherLimit) {
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::DESCENDING;
  Order order;
  order.columns() = {oc};
  Page page;
  page.limit() = 200;
  page.offset() = 0;
  auto query = TestUtils::prepareQuery(std::string("host"), order, page);
  auto result = getData(*model, query);
  EXPECT_EQ(5, result.rows()->size());
  EXPECT_EQ(5, *result.totalCount());
}

TEST_F(ModelServerTest, ModelPageHigherOffset) {
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::DESCENDING;
  Order order;
  order.columns() = {oc};
  Page page;
  page.limit() = 1;
  page.offset() = 100;
  auto query = TestUtils::prepareQuery(std::string("host"), order, page);
  auto result = getData(*model, query);
  EXPECT_EQ(0, result.rows()->size());
}

TEST_F(ModelServerTest, ModelTypeHeadBasic) {
  auto request = TestUtils::prepareTypeheadRequest(
      std::string("host"), std::string(""), 10);
  auto result = getTypeahead(*model, request);
  const std::vector<std::string> expected = {
      "host0", "host1", "host2", "host3", "host4"};
  EXPECT_EQ(expected, *result.matches());
}

TEST_F(ModelServerTest, ModelTypeHeadBasicLimit) {
  auto request = TestUtils::prepareTypeheadRequest(
      std::string("host"), std::string("ho"), 3);
  auto result = getTypeahead(*model, request);
  const std::vector<std::string> expected = {"host0", "host1", "host2"};
  EXPECT_EQ(expected, *result.matches());
}

TEST_F(ModelServerTest, ModelTypeHeadNoResult) {
  auto request = TestUtils::prepareTypeheadRequest(
      std::string("host"), std::string("hot"), 3);
  auto result = getTypeahead(*model, request);
  EXPECT_EQ(0, result.matches()->size());
}

TEST_F(ModelServerTest, ModelTypeHeadContainer) {
  auto request = TestUtils::prepareTypeheadRequest(
      std::string("rack"), std::string("rac"), 2);
  auto result = getTypeahead(*model, request);
  const std::vector<std::string> expected = {"rack0", "rack1"};
  EXPECT_EQ(expected, *result.matches());
}

TEST_F(ModelServerTest, ModelTypeHeadScope) {
  auto request =
      TestUtils::prepareTypeheadRequest(std::string("msb"), std::string(""), 2);
  auto result = getTypeahead(*model, request);
  const std::vector<std::string> expected = {"msb0", "msb1"};
  EXPECT_EQ(expected, *result.matches());
}

TEST_F(ModelServerTest, ModelTypeaheadPartition) {
  auto request = TestUtils::prepareTypeheadRequest(
      std::string("scheme"), std::string(""), 10);
  auto result = getTypeahead(*model, request);
  const std::vector<std::string> expected = {"twshared", "cache"};
  EXPECT_EQ(makeSet(expected), makeSet(*result.matches()));
}

TEST_F(ModelServerTest, ModelTypeaheadEqSetsPartition) {
  auto request = TestUtils::prepareTypeheadRequest(
      std::string(kEquivSetPartition), std::string(""), 10);
  auto result = getTypeahead(*model, request);

  // all hosts are in the same equivalence set
  const std::vector<std::string> expected = {"equiv_set_0"};
  EXPECT_EQ(makeSet(expected), makeSet(*result.matches()));
}

TEST_F(ModelServerTest, MovesBetweenAssignment) {
  std::map<std::string, std::string> variableToContainerOverride = {};

  Assignment source;
  source.base() = AssignmentBase::INITIAL;
  source.variableToContainerOverride() = variableToContainerOverride;

  Assignment dst;
  dst.base() = AssignmentBase::FINAL;
  dst.variableToContainerOverride() = variableToContainerOverride;
  auto request = TestUtils::prepareMoveBetweenAssigmentRequest(
      std::move(source), std::move(dst));
  auto result = model->getMovesBetweenAssignments(request);
  const std::map<std::string, std::string> expected = {{"host0", "rack2"}};
  EXPECT_EQ(expected, *result.variableToContainer());
}

TEST_F(ModelServerTest, MovesBetweenAssignmentExtraMoves) {
  std::map<std::string, std::string> variableToContainerOverride = {
      {"host1", "rack2"}};

  Assignment source;
  source.base() = AssignmentBase::INITIAL;
  source.variableToContainerOverride() = std::move(variableToContainerOverride);

  Assignment dst;
  dst.base() = AssignmentBase::FINAL;
  dst.variableToContainerOverride() = variableToContainerOverride;
  auto request = TestUtils::prepareMoveBetweenAssigmentRequest(
      std::move(source), std::move(dst));
  auto result = model->getMovesBetweenAssignments(request);
  const std::map<std::string, std::string> expected = {
      {"host0", "rack2"}, {"host1", "rack0"}};
  EXPECT_EQ(expected, *result.variableToContainer());
}

TEST_F(ModelServerTest, MetricDistribution) {
  MetricDistributionRequest request;
  request.entity() = "host";
  request.metric() = "ram";
  request.maxPoints() = 1000;

  std::vector<MetricDistributionPoint> points;
  {
    MetricDistributionPoint point;
    point.index() = 0;
    point.metricValue() = 128000;
    points.push_back(std::move(point));
  }

  {
    MetricDistributionPoint point;
    point.index() = 1;
    point.metricValue() = 128000;
    points.push_back(std::move(point));
  }

  {
    MetricDistributionPoint point;
    point.index() = 2;
    point.metricValue() = 128000;
    points.push_back(std::move(point));
  }

  {
    MetricDistributionPoint point;
    point.index() = 3;
    point.metricValue() = 64000;
    points.push_back(std::move(point));
  }

  {
    MetricDistributionPoint point;
    point.index() = 4;
    point.metricValue() = 32000;
    points.push_back(std::move(point));
  }

  auto response = getMetricDistribution(*model, request);
  EXPECT_EQ(points, *response.points());
}

TEST_F(ModelServerTest, MetricDistributionLimit) {
  MetricDistributionRequest request;
  request.entity() = "host";
  request.metric() = "ram";
  request.maxPoints() = 2;

  std::vector<MetricDistributionPoint> points;
  {
    MetricDistributionPoint point;
    point.index() = 0;
    point.metricValue() = 128000;
    points.push_back(std::move(point));
  }

  {
    MetricDistributionPoint point;
    point.index() = 4;
    point.metricValue() = 32000;
    points.push_back(std::move(point));
  }

  auto response = getMetricDistribution(*model, request);
  EXPECT_EQ(points, *response.points());
}

TEST_F(ModelServerTest, getProblemMetadata) {
  auto result = modelWithMovesSummary->getProblemMetadata();
  // we expect 2 partitions, because we added one and there is always an
  // additional one that is created w.r.t. equivalence sets in the problem
  const auto& partitionNames = *result.partitionNames();
  EXPECT_EQ(2, partitionNames.size());
  const std::set<std::string> expectedPartitionNames = {
      "scheme", kEquivSetPartition.data()};
  EXPECT_EQ(
      std::set<std::string>(partitionNames.begin(), partitionNames.end()),
      expectedPartitionNames);
  const auto& scopeNames = *result.scopeNames();
  EXPECT_EQ(3, scopeNames.size());
  const std::set<std::string> expectedScopeNames = {
      "msb", "row", "row_incomplete"};
  EXPECT_EQ(
      expectedScopeNames,
      std::set<std::string>(scopeNames.begin(), scopeNames.end()));
  EXPECT_EQ(result.objectName().value(), "host");
  EXPECT_EQ(result.containerName().value(), "rack");
  EXPECT_EQ(result.variableName().value(), "host");
  EXPECT_EQ(result.hasFinalAssignment(), true);
  EXPECT_EQ(result.hasIntermediateAssignment().value(), true);
  EXPECT_EQ(result.numSteps().value(), 3);
  ASSERT_EQ(result.dynamicDimensionNames().value().size(), 1);
  EXPECT_EQ(result.dynamicDimensionNames().value().at(0), "dynamicLoad");
  EXPECT_FALSE(*result.hasOnlySingleMoves());
}

TEST_F(ModelServerTest, getProblemMetadataFingerprints) {
  auto result1 = model->getProblemMetadata();
  auto result2 = model->getProblemMetadata();

  // Fingerprints are non-empty hex strings
  EXPECT_FALSE(result1.objectNamesFingerprint()->empty());
  EXPECT_FALSE(result1.containerNamesFingerprint()->empty());

  // Fingerprints are deterministic (same model produces same fingerprints)
  EXPECT_EQ(
      *result1.objectNamesFingerprint(), *result2.objectNamesFingerprint());
  EXPECT_EQ(
      *result1.containerNamesFingerprint(),
      *result2.containerNamesFingerprint());

  // Object and container fingerprints differ (different entity sets)
  EXPECT_NE(
      *result1.objectNamesFingerprint(), *result1.containerNamesFingerprint());
}

#ifndef REBALANCER_OSS_BUILD
TEST_F(ModelServerTest, editProblem) {
  if (folly::kIsDebug) {
    GTEST_SKIP()
        << "Skipped in dev mode: reads from and writes to Manifold and can be slow.";
  }
  DeleteEditProblemRequest toDelete;
  toDelete.constraints() = {"constraint1"};
  toDelete.goals() = {"goal1", "soft_constraint1"};
  EditProblemRequest request;
  request.toDelete() = toDelete;

  auto modelWithConstraintsAndGoals = getModelWithConstraintsAndGoals();
  auto result = modelWithConstraintsAndGoals->editProblem(request);
  auto updatedManifoldId = *result.manifoldId();

  auto bundle = Manifold::download(updatedManifoldId);
  auto model = LoadModel::buildData(std::move(bundle));
  auto thriftUniverse = model.universe->toThrift();
  // "constraint1" and "soft_constraint1" were deleted from constraints
  // only "constraints2" and "initially_satisfied_constraint1" left
  EXPECT_EQ(thriftUniverse.idStore().value().constraintIds().value().size(), 2);
  EXPECT_EQ(
      thriftUniverse.idStore().value().constraintIds().value().at(0),
      model.universe->getConstraintId("constraint2").asInt());
  // "goal1" was deleted, only "goal2" left
  EXPECT_EQ(thriftUniverse.idStore().value().goalIds().value().size(), 1);
  EXPECT_EQ(
      thriftUniverse.idStore().value().goalIds().value().at(0),
      model.universe->getGoalId("goal2").asInt());

  // clean up updated problem from manifold
  Manifold::deleteObj(updatedManifoldId);
}

CO_TEST_F(ModelServerTest, ExportTableBasic) {
  CO_SKIP_IF(
      folly::kIsDebug,
      "Skipped in dev mode: writes to and reads from Hive and can be slow.");
  // Create a request to export the "host" table
  ExportTableRequest request;
  request.tableName() = "host";
  const auto response = co_await folly::coro::timeout(
      model->exportTable(request), std::chrono::seconds(300));

  // Verify that the table name follows the expected format:
  // {tableName}_*
  const std::string& prestoTableName = *response.tableName();
  const std::string expectedPrefix = fmt::format("{}_", *request.tableName());
  EXPECT_TRUE(prestoTableName.starts_with(expectedPrefix));

  // Verify that the table has the expected columns and rows
  const auto selectQuery = fmt::format("SELECT * FROM {}", prestoTableName);
  facebook::datainfra::presto_next::PrestoRequest prestoRequest;
  prestoRequest.sql() = selectQuery;
  prestoRequest.schema() = *response.tableNamespace();
  prestoRequest.user() = "rebalancer_explorer_table";
  prestoRequest.source() = "rebalancer_explorer_table";
  prestoRequest.oncall() = "algopt";
  prestoRequest.client_timeout_ms() = 120000;
  const auto prestoResult = co_await folly::coro::timeout(
      facebook::datainfra::presto_next::ligen::PrestoQuery::coro_run(
          std::move(prestoRequest)),
      std::chrono::seconds(180));

  // Extract column names from the result
  std::set<std::string> columnNames;
  for (const auto& column : *prestoResult.columns()) {
    auto [_, isInserted] = columnNames.insert(*column.name());
    // we don't expect duplicate column names
    EXPECT_TRUE(isInserted);
  }

  const std::vector<std::string> expectedUnsanitizedColumnNames = {
      "ds", // extra column included in presto
      "host",
      "is_movable",
      "host_count",
      "network_0",
      "network_1",
      "network_2",
      "ram",
      "scheme",
      "src.dynamicload",
      "dst.dynamicload",
      "src.rack",
      "dst.rack",
      "src.row",
      "dst.row",
      "src.msb",
      "dst.msb",
      "equivalence_set_partition",
  };
  // Sanitize the expected column names to match Presto's format
  for (auto& name : expectedUnsanitizedColumnNames) {
    EXPECT_TRUE(columnNames.contains(PrestoUtils::getSanitizedOrThrow(name)));
  }

  // expect 5 rows
  EXPECT_EQ(5, prestoResult.rows()->size());

  // delete the table at the end of the test
  facebook::datainfra::presto_next::PrestoRequest deleteRequest;
  deleteRequest.sql() = fmt::format("DROP TABLE IF EXISTS {}", prestoTableName);
  deleteRequest.schema() = *response.tableNamespace();
  deleteRequest.user() = "rebalancer_explorer_table";
  deleteRequest.source() = "rebalancer_explorer_table";
  deleteRequest.oncall() = "algopt";
  deleteRequest.client_timeout_ms() = 120000;
  co_await folly::coro::timeout(
      facebook::datainfra::presto_next::ligen::PrestoQuery::coro_run(
          std::move(deleteRequest)),
      std::chrono::seconds(180));
}
#endif // REBALANCER_OSS_BUILD

TEST_F(ModelServerTest, MoveSetsNoMovesSumary) {
  const auto request = TestUtils::prepareMoveSetsRequest(0, 1);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      model->getMoveSets(request), "No movesSummary in AssignmentSolution");
}

TEST_F(ModelServerTest, MoveSetsNonEmptyVariableToContainerOverride) {
  auto request = TestUtils::prepareMoveSetsRequest(0, 1);
  request.assignmentA()->variableToContainerOverride() = {{"host0", "rack1"}};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      modelWithMovesSummary->getMoveSets(request),
      "Expected empty variableToContainerOverride in assignments");
}

TEST_F(ModelServerTest, MoveSetsNegativeStartMoveSetIdx) {
  const auto request = TestUtils::prepareMoveSetsRequest(-1, 1);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      modelWithMovesSummary->getMoveSets(request),
      "startMoveSetIdx or endMoveSetIdx cannot be negative");
}

TEST_F(ModelServerTest, MoveSetsNegativeEndMoveSetIdx) {
  const auto request = TestUtils::prepareMoveSetsRequest(0, -1);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      modelWithMovesSummary->getMoveSets(request),
      "startMoveSetIdx or endMoveSetIdx cannot be negative");
}

TEST_F(ModelServerTest, MoveSetsStartEqualEnd) {
  const auto request = TestUtils::prepareMoveSetsRequest(1, 1);
  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // verify that the table is empty
  EXPECT_EQ(0, table.rows()->size());
  EXPECT_EQ(0, *table.totalCount());
}

TEST_F(ModelServerTest, MoveSetsStartLargerThanEnd) {
  const auto request = TestUtils::prepareMoveSetsRequest(2, 1);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      modelWithMovesSummary->getMoveSets(request),
      "startMoveSetIdx should be at most endMoveSetIdx");
}

TEST_F(ModelServerTest, MoveSetsEndLargerThanAvailable) {
  const auto request = TestUtils::prepareMoveSetsRequest(0, 4);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      modelWithMovesSummary->getMoveSets(request),
      "endMoveSetIdx must be at most the number of moves (3)");
}

TEST_F(ModelServerTest, MoveSetsBasic) {
  const auto request = TestUtils::prepareMoveSetsRequest(0, 2);
  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // Verify we have the expected columns
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 5);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Object");
  EXPECT_EQ(columns.at(2).name().value(), "Source Container");
  EXPECT_EQ(columns.at(3).name().value(), "Destination Container");
  EXPECT_EQ(columns.at(4).name().value(), "Cycle Id");

  // Verify column types
  EXPECT_EQ(columns.at(0).type().value(), ColumnType::IDENTIFIER);
  EXPECT_EQ(columns.at(1).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(2).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(3).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(4).type().value(), ColumnType::INTEGER);

  // Verify we have rows for moves in movesets 0 and 1; we expect 6 rows
  EXPECT_EQ(table.rows()->size(), 6);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  // Verify each row has the correct number of cells
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 5);
    EXPECT_TRUE(cells.at(0).doubleValue().has_value());
    EXPECT_TRUE(cells.at(1).stringValue().has_value());
    EXPECT_TRUE(cells.at(2).stringValue().has_value());
    EXPECT_TRUE(cells.at(3).stringValue().has_value());
  }

  // Collect and verify the actual move data
  std::map<int, std::vector<std::tuple<std::string, std::string, std::string>>>
      movesBySet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    const auto moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    const auto object = *cells.at(1).stringValue();
    const auto srcContainer = *cells.at(2).stringValue();
    const auto dstContainer = *cells.at(3).stringValue();
    movesBySet[moveSetNum].emplace_back(object, srcContainer, dstContainer);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(movesBySet.size(), 2);
  EXPECT_TRUE(movesBySet.contains(1));
  EXPECT_TRUE(movesBySet.contains(2));

  // Verify moveset 1 has 3 moves: host0, host1, host2 from rack0 to rack1
  EXPECT_EQ(movesBySet[1].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string>>
      expectedMovesSet0 = {
          {"host0", "rack0", "rack1"},
          {"host1", "rack0", "rack1"},
          {"host2", "rack0", "rack1"}};
  const std::set<std::tuple<std::string, std::string, std::string>>
      actualMovesSet0(movesBySet[1].begin(), movesBySet[1].end());
  EXPECT_EQ(actualMovesSet0, expectedMovesSet0);

  // Verify moveset 2 has 3 moves: host0 rack1->rack2, host3 rack1->rack2, host2
  // rack1->rack0
  EXPECT_EQ(movesBySet[2].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string>>
      expectedMovesSet1 = {
          {"host0", "rack1", "rack2"},
          {"host3", "rack1", "rack2"},
          {"host2", "rack1", "rack0"}};
  const std::set<std::tuple<std::string, std::string, std::string>>
      actualMovesSet1(movesBySet[2].begin(), movesBySet[2].end());
  EXPECT_EQ(actualMovesSet1, expectedMovesSet1);
}

TEST_F(ModelServerTest, MoveSetsWithPartition) {
  const auto request =
      TestUtils::prepareMoveSetsRequest(0, 2, /*partitionName=*/"scheme");
  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // Verify we have the expected columns with partition
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 6);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Group");
  EXPECT_EQ(columns.at(2).name().value(), "Source Container");
  EXPECT_EQ(columns.at(3).name().value(), "Destination Container");
  EXPECT_EQ(columns.at(4).name().value(), "Object Count");
  EXPECT_EQ(columns.at(5).name().value(), "Cycle Id");

  // Verify column types
  EXPECT_EQ(columns.at(0).type().value(), ColumnType::IDENTIFIER);
  EXPECT_EQ(columns.at(1).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(2).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(3).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(4).type().value(), ColumnType::INTEGER);
  EXPECT_EQ(
      columns.at(4).description(),
      "Value in row-i is the number of objects of the corresponding group in row-i that moved from source to destination");

  // Verify we have rows - expect fewer rows than basic since we're grouping by
  // partition
  EXPECT_EQ(table.rows()->size(), 5);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  // Verify each row has the correct number of cells and all values are present
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 6);
    EXPECT_TRUE(cells.at(0).doubleValue().has_value());
    EXPECT_TRUE(cells.at(1).stringValue().has_value());
    EXPECT_TRUE(cells.at(2).stringValue().has_value());
    EXPECT_TRUE(cells.at(3).stringValue().has_value());
    EXPECT_TRUE(cells.at(4).doubleValue().has_value());
    EXPECT_GT(*cells.at(4).doubleValue(), 0.0);
  }

  // Collect and verify the actual move data grouped by partition
  std::map<
      int,
      std::vector<std::tuple<std::string, std::string, std::string, double>>>
      movesBySet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    const auto moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    const auto group = *cells.at(1).stringValue();
    const auto srcContainer = *cells.at(2).stringValue();
    const auto dstContainer = *cells.at(3).stringValue();
    const auto objectCount = *cells.at(4).doubleValue();

    movesBySet[moveSetNum].emplace_back(
        group, srcContainer, dstContainer, objectCount);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(movesBySet.size(), 2);
  EXPECT_TRUE(movesBySet.contains(1));
  EXPECT_TRUE(movesBySet.contains(2));

  // Verify the moves are grouped by partition correctly
  // Moveset 1: 3 groups moving from rack0->rack1
  // - host0 (twshared partition) -> grouped as "twshared"
  // - host1 (cache partition) -> grouped as "cache"
  // - host2 (not in any partition) -> grouped as "Not in partition (object:
  // host2)"
  EXPECT_EQ(movesBySet[1].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      expectedMovesSet0 = {
          {"twshared", "rack0", "rack1", 1.0},
          {"cache", "rack0", "rack1", 1.0},
          {"Not in partition (object: host2)", "rack0", "rack1", 1.0}};
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      actualMovesSet0(movesBySet[1].begin(), movesBySet[1].end());
  EXPECT_EQ(actualMovesSet0, expectedMovesSet0);

  // Moveset 2: 2 groups with different destinations
  // - host0 and host3 (both twshared partition) rack1->rack2 -> grouped as
  // "twshared" with count 2
  // - host2 (not in any partition) rack1->rack0 -> grouped as "Not in partition
  // (object: host2)"
  EXPECT_EQ(movesBySet[2].size(), 2);
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      expectedMovesSet1 = {
          {"twshared", "rack1", "rack2", 2.0},
          {"Not in partition (object: host2)", "rack1", "rack0", 1.0}};
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      actualMovesSet1(movesBySet[2].begin(), movesBySet[2].end());
  EXPECT_EQ(actualMovesSet1, expectedMovesSet1);
}

TEST_F(ModelServerTest, MoveSetsWithEqSetPartition) {
  // verify that move sets table works when equivalence set partition is used.
  // This test just verifies that the table is generated correctly and not the
  // values themselves.
  const auto request = TestUtils::prepareMoveSetsRequest(
      /*startMoveSetIdx=*/0,
      /*endMoveSetIdx=*/2,
      /*partitionName=*/std::string(kEquivSetPartition));
  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // Verify we have the expected columns with partition
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 6);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Group");
  EXPECT_EQ(columns.at(2).name().value(), "Source Container");
  EXPECT_EQ(columns.at(3).name().value(), "Destination Container");
  EXPECT_EQ(columns.at(4).name().value(), "Object Count");
  EXPECT_EQ(columns.at(5).name().value(), "Cycle Id");
}

TEST_F(ModelServerTest, MoveSetsWithScope) {
  const auto request =
      TestUtils::prepareMoveSetsRequest(0, 2, std::nullopt, "row");
  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // Verify we have the expected columns with scope
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 5);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Object");
  EXPECT_EQ(columns.at(2).name().value(), "Source ScopeItem");
  EXPECT_EQ(columns.at(3).name().value(), "Destination ScopeItem");
  EXPECT_EQ(columns.at(4).name().value(), "Cycle Id");

  // Verify column types
  EXPECT_EQ(columns.at(0).type().value(), ColumnType::IDENTIFIER);
  EXPECT_EQ(columns.at(1).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(2).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(3).type().value(), ColumnType::STRING);

  // Verify we have rows - should be same as basic since we're not grouping
  EXPECT_EQ(table.rows()->size(), 6);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  // Verify each row has the correct number of cells and all values are present
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 5);
    EXPECT_TRUE(cells.at(0).doubleValue().has_value());
    EXPECT_TRUE(cells.at(1).stringValue().has_value());
    EXPECT_TRUE(cells.at(2).stringValue().has_value());
    EXPECT_TRUE(cells.at(3).stringValue().has_value());
  }

  // Collect and verify the actual move data with scope items
  std::map<int, std::vector<std::tuple<std::string, std::string, std::string>>>
      movesBySet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    const auto moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    const auto object = *cells.at(1).stringValue();
    const auto srcScopeItem = *cells.at(2).stringValue();
    const auto dstScopeItem = *cells.at(3).stringValue();
    movesBySet[moveSetNum].emplace_back(object, srcScopeItem, dstScopeItem);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(movesBySet.size(), 2);
  EXPECT_TRUE(movesBySet.contains(1));
  EXPECT_TRUE(movesBySet.contains(2));

  // Verify moveset 1 has 3 moves with correct scope mappings
  // Based on the test setup: rack0->row0, rack1->row1, rack2->row1
  EXPECT_EQ(movesBySet[1].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string>>
      expectedMovesSet0 = {
          {"host0", "row0", "row1"}, // host0: rack0->rack1 becomes row0->row1
          {"host1", "row0", "row1"}, // host1: rack0->rack1 becomes row0->row1
          {"host2", "row0", "row1"} // host2: rack0->rack1 becomes row0->row1
      };
  const std::set<std::tuple<std::string, std::string, std::string>>
      actualMovesSet0(movesBySet[1].begin(), movesBySet[1].end());
  EXPECT_EQ(actualMovesSet0, expectedMovesSet0);

  // Verify moveset 2 has 3 moves with correct scope mappings
  EXPECT_EQ(movesBySet[2].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string>>
      expectedMovesSet1 = {
          {"host0", "row1", "row1"}, // host0: rack1->rack2 becomes row1->row1
          {"host3", "row1", "row1"}, // host3: rack1->rack2 becomes row1->row1
          {"host2", "row1", "row0"} // host2: rack1->rack0 becomes row1->row0
      };
  const std::set<std::tuple<std::string, std::string, std::string>>
      actualMovesSet1(movesBySet[2].begin(), movesBySet[2].end());
  EXPECT_EQ(actualMovesSet1, expectedMovesSet1);
}

TEST_F(ModelServerTest, MoveSetsWithPartitionAndScope) {
  const auto request =
      TestUtils::prepareMoveSetsRequest(0, 2, "scheme", "row_incomplete");
  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // Verify we have the expected columns with both partition and scope
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 6);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Group");
  EXPECT_EQ(columns.at(2).name().value(), "Source ScopeItem");
  EXPECT_EQ(columns.at(3).name().value(), "Destination ScopeItem");
  EXPECT_EQ(columns.at(4).name().value(), "Object Count");
  EXPECT_EQ(columns.at(5).name().value(), "Cycle Id");

  // Verify column types
  EXPECT_EQ(columns.at(0).type().value(), ColumnType::IDENTIFIER);
  EXPECT_EQ(columns.at(1).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(2).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(3).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(4).type().value(), ColumnType::INTEGER);
  EXPECT_EQ(
      columns.at(4).description(),
      "Value in row-i is the number of objects of the corresponding group in row-i that moved from source to destination");

  // Verify we have rows - expect fewer rows than basic since we're grouping by
  // partition and using scope
  EXPECT_EQ(table.rows()->size(), 5);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  // Verify each row has the correct number of cells and all values are present
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 6);
    EXPECT_TRUE(cells.at(0).doubleValue().has_value());
    EXPECT_TRUE(cells.at(1).stringValue().has_value());
    EXPECT_TRUE(cells.at(2).stringValue().has_value());
    EXPECT_TRUE(cells.at(3).stringValue().has_value());
    EXPECT_TRUE(cells.at(4).doubleValue().has_value());
    EXPECT_GT(*cells.at(4).doubleValue(), 0.0);
  }

  // Collect and verify the actual move data grouped by partition with scope
  // items
  std::map<
      int,
      std::vector<std::tuple<std::string, std::string, std::string, double>>>
      movesBySet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    const auto moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    const auto group = *cells.at(1).stringValue();
    const auto srcScopeItem = *cells.at(2).stringValue();
    const auto dstScopeItem = *cells.at(3).stringValue();
    const auto objectCount = *cells.at(4).doubleValue();
    movesBySet[moveSetNum].emplace_back(
        group, srcScopeItem, dstScopeItem, objectCount);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(movesBySet.size(), 2);
  EXPECT_TRUE(movesBySet.contains(1));
  EXPECT_TRUE(movesBySet.contains(2));

  // Verify the moves are grouped by partition with scope mappings
  // Based on the test setup: rack0->row0, rack1->row1, rack2->row1
  EXPECT_EQ(movesBySet[1].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      expectedMovesSet0 = {
          {"twshared", "row0", "row1", 1.0}, // rack0->rack1 becomes row0->row1
          {"cache", "row0", "row1", 1.0}, // rack0->rack1 becomes row0->row1
          {"Not in partition (object: host2)",
           "row0",
           "row1",
           1.0}}; // rack0->rack1 becomes row0->row1
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      actualMovesSet0(movesBySet[1].begin(), movesBySet[1].end());
  EXPECT_EQ(actualMovesSet0, expectedMovesSet0);

  EXPECT_EQ(movesBySet[2].size(), 2);
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      expectedMovesSet1 = {
          {"twshared",
           "row1",
           "Out of scope (container: rack2)",
           2.0}, // rack1->rack2 becomes row1->Out of scope (container: rack2)
          {"Not in partition (object: host2)",
           "row1",
           "row0",
           1.0}}; // rack1->rack0 becomes row1->row0
  const std::set<std::tuple<std::string, std::string, std::string, double>>
      actualMovesSet1(movesBySet[2].begin(), movesBySet[2].end());
  EXPECT_EQ(actualMovesSet1, expectedMovesSet1);
}

TEST_F(ModelServerTest, MoveSetsBasicGroupBy) {
  auto request = TestUtils::prepareMoveSetsRequest(0, 2);
  Group group{};
  group.columns() = {"Source Container", "Destination Container"};
  Query query;
  query.group() = std::move(group);
  request.query() = std::move(query);

  const auto table = *modelWithMovesSummary->getMoveSets(request).table();

  // Verify we have the expected columns
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 3);
  EXPECT_EQ(columns.at(0).name().value(), "Source Container");
  EXPECT_EQ(columns.at(1).name().value(), "Destination Container");
  EXPECT_EQ(columns.at(2).name().value(), "Row_Count");

  // Verify column types
  EXPECT_EQ(columns.at(0).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(1).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(2).type().value(), ColumnType::INTEGER);

  EXPECT_EQ(table.rows()->size(), 3);
  EXPECT_EQ(3, *table.totalCount());

  // Collect and verify actual row data
  std::set<std::tuple<std::string, std::string, double>> actualRows;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 3);
    EXPECT_TRUE(cells.at(0).stringValue().has_value());
    EXPECT_TRUE(cells.at(1).stringValue().has_value());
    EXPECT_TRUE(cells.at(2).doubleValue().has_value());

    const auto srcContainer = *cells.at(0).stringValue();
    const auto dstContainer = *cells.at(1).stringValue();
    const auto moveSetUniqueCount = *cells.at(2).doubleValue();
    actualRows.emplace(srcContainer, dstContainer, moveSetUniqueCount);
  }

  EXPECT_EQ(actualRows.size(), 3);
  const std::set<std::tuple<std::string, std::string, double>> expectedRows = {
      {"rack0", "rack1", 3}, {"rack1", "rack2", 2}, {"rack1", "rack0", 1}};
  EXPECT_EQ(expectedRows, actualRows);
}

TEST_F(ModelServerTest, MoveSetsBasicWithStageSolver) {
  const auto request = TestUtils::prepareMoveSetsRequest(0, 2);
  const auto table =
      *modelWithMovesSummaryAndStageSolver->getMoveSets(request).table();

  // Verify we have the expected columns
  const auto& columns = *table.columns();
  ASSERT_EQ(columns.size(), 6);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Object");
  EXPECT_EQ(columns.at(2).name().value(), "Source Container");
  EXPECT_EQ(columns.at(3).name().value(), "Destination Container");
  EXPECT_EQ(columns.at(4).name().value(), "Stage Id");
  EXPECT_EQ(columns.at(5).name().value(), "Cycle Id");

  // Verify column types
  EXPECT_EQ(columns.at(0).type().value(), ColumnType::IDENTIFIER);
  EXPECT_EQ(columns.at(1).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(2).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(3).type().value(), ColumnType::STRING);
  EXPECT_EQ(columns.at(4).type().value(), ColumnType::INTEGER);
  EXPECT_EQ(columns.at(5).type().value(), ColumnType::INTEGER);

  EXPECT_EQ(
      "Value w.r.t. a moveSet is the local search stage during which the moveSet was applied",
      *columns.at(4).description());

  // Verify we have rows for moves in movesets 0 and 1; we expect 6 rows
  EXPECT_EQ(table.rows()->size(), 6);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  // Collect and verify the actual move data
  std::map<
      int,
      std::vector<std::tuple<std::string, std::string, std::string, int>>>
      movesBySet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    ASSERT_TRUE(cells.at(0).doubleValue().has_value());
    ASSERT_TRUE(cells.at(1).stringValue().has_value());
    ASSERT_TRUE(cells.at(2).stringValue().has_value());
    ASSERT_TRUE(cells.at(3).stringValue().has_value());
    ASSERT_TRUE(cells.at(4).doubleValue().has_value());
    ASSERT_TRUE(cells.at(5).doubleValue().has_value());
    const auto moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    const auto object = *cells.at(1).stringValue();
    const auto srcContainer = *cells.at(2).stringValue();
    const auto dstContainer = *cells.at(3).stringValue();
    const auto stageId = static_cast<int>(*cells.at(4).doubleValue());
    const auto cycleId = static_cast<int>(*cells.at(5).doubleValue());
    EXPECT_GE(cycleId, 1);
    movesBySet[moveSetNum].emplace_back(
        object, srcContainer, dstContainer, stageId);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(movesBySet.size(), 2);
  EXPECT_TRUE(movesBySet.contains(1));
  EXPECT_TRUE(movesBySet.contains(2));

  // Verify moveset 1 has 3 moves: host0, host1, host2 from rack0 to rack1
  EXPECT_EQ(movesBySet[1].size(), 3);
  const auto moveSet1StageId = 0;
  const std::set<std::tuple<std::string, std::string, std::string, int>>
      expectedMovesSet1 = {
          {"host0", "rack0", "rack1", moveSet1StageId},
          {"host1", "rack0", "rack1", moveSet1StageId},
          {"host2", "rack0", "rack1", moveSet1StageId}};
  const std::set<std::tuple<std::string, std::string, std::string, int>>
      actualMovesSet1(movesBySet[1].begin(), movesBySet[1].end());
  EXPECT_EQ(actualMovesSet1, expectedMovesSet1);

  // Verify moveset 2 has 3 moves: host0 rack1->rack2, host3 rack1->rack2, host2
  // rack1->rack0
  const auto moveSet2StageId = 1;
  EXPECT_EQ(movesBySet[2].size(), 3);
  const std::set<std::tuple<std::string, std::string, std::string, int>>
      expectedMovesSet2 = {
          {"host0", "rack1", "rack2", moveSet2StageId},
          {"host3", "rack1", "rack2", moveSet2StageId},
          {"host2", "rack1", "rack0", moveSet2StageId}};
  const std::set<std::tuple<std::string, std::string, std::string, int>>
      actualMovesSet2(movesBySet[2].begin(), movesBySet[2].end());
  EXPECT_EQ(actualMovesSet2, expectedMovesSet2);
}

TEST_F(ModelServerTest, MoveSetsWithObjectiveNamesAndMoveStatsSpec) {
  MoveStatsSpec moveStatsSpec;
  moveStatsSpec.trackContainers() = true;
  moveStatsSpec.showAllChangedObjectivesInMovesSummary() = true;

  auto model = getModelWithConstraintsAndGoals({std::move(moveStatsSpec)});
  // check objective names are as expected;  we have three goals and three
  // initially broken constraints; there is one initially satisfied constraint
  // and that should not beincluded
  auto result = model->getProblemMetadata();
  const std::set<std::string> expectedObjectiveNames = {
      "goal1", "goal2", "constraint1", "constraint2", "soft_constraint1"};
  const std::set<std::string> actualObjectiveNames(
      result.objectiveNames()->begin(), result.objectiveNames()->end());
  EXPECT_EQ(expectedObjectiveNames, actualObjectiveNames);
  EXPECT_TRUE(*result.canDisplayObjChangesInMoveSetsTable());

  // Now test the MoveSets functionality with only two objective names
  auto request = TestUtils::prepareMoveSetsRequest(0, 2);
  request.objectiveNames() =
      std::vector<std::string>{"goal1", "soft_constraint1"};

  const auto response = model->getMoveSets(request);
  const auto& table = *response.table();
  const auto& columns = *table.columns();
  EXPECT_EQ(
      columns.size(), 7); // 4 base columns + 2 objective columns + Cycle Id
  EXPECT_EQ(*columns.at(0).name(), "MoveSet #");
  EXPECT_EQ(*columns.at(1).name(), "goal1 (\u0394)");
  EXPECT_EQ(*columns.at(2).name(), "soft_constraint1 (\u0394)");
  EXPECT_EQ(*columns.at(3).name(), "Object");
  EXPECT_EQ(*columns.at(4).name(), "Source Container");
  EXPECT_EQ(*columns.at(5).name(), "Destination Container");
  EXPECT_EQ(*columns.at(6).name(), "Cycle Id");

  EXPECT_EQ(*columns.at(0).type(), ColumnType::IDENTIFIER);
  EXPECT_EQ(*columns.at(1).type(), ColumnType::DOUBLE);
  EXPECT_EQ(*columns.at(2).type(), ColumnType::DOUBLE);
  EXPECT_EQ(*columns.at(3).type(), ColumnType::STRING);
  EXPECT_EQ(*columns.at(4).type(), ColumnType::STRING);
  EXPECT_EQ(*columns.at(5).type(), ColumnType::STRING);
  EXPECT_EQ(*columns.at(6).type(), ColumnType::INTEGER);

  // Verify objective column descriptions
  EXPECT_EQ(
      *columns.at(1).description(),
      "Value w.r.t. moveSet i is the change in value of this objective as a result of applying moveSet i");
  EXPECT_EQ(
      *columns.at(2).description(),
      "Value w.r.t. moveSet i is the change in value of this objective as a result of applying moveSet i");

  // Verify we have rows for moves in movesets 0 and 1; we expect 5 rows (3 from
  // first moveset, 1 from second)
  EXPECT_EQ(table.rows()->size(), 4);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  std::map<int, std::vector<std::vector<CellData>>> rowsByMoveSet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 7);
    const int moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    rowsByMoveSet[moveSetNum].push_back(cells);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(rowsByMoveSet.size(), 2);
  EXPECT_TRUE(rowsByMoveSet.contains(1));
  EXPECT_TRUE(rowsByMoveSet.contains(2));

  // Verify moveset 1 has 3 moves
  EXPECT_EQ(rowsByMoveSet[1].size(), 3);
  for (const auto& cells : rowsByMoveSet[1]) {
    EXPECT_EQ(static_cast<int>(*cells.at(0).doubleValue()), 1);

    // Verify objective values
    // NOTE: moves summary records change as (old - new), so values are
    // multiplied by -1
    EXPECT_NEAR(*cells.at(1).doubleValue(), -8.0, 1e-8); // goal1
    EXPECT_NEAR(*cells.at(2).doubleValue(), -3.7, 1e-8); // soft_constraint1

    const auto& object = *cells.at(3).stringValue();
    const auto& srcContainer = *cells.at(4).stringValue();
    const auto& dstContainer = *cells.at(5).stringValue();
    EXPECT_EQ(srcContainer, "host0");
    EXPECT_EQ(dstContainer, "host1");
    EXPECT_TRUE(object == "task0" || object == "task1" || object == "task2");
  }

  // moveset 2 has 1 move
  EXPECT_EQ(rowsByMoveSet[2].size(), 1);
  const auto& cells = rowsByMoveSet[2][0];
  EXPECT_EQ(static_cast<int>(*cells.at(0).doubleValue()), 2);

  // Verify objective values
  // NOTE: moves summary records change as (old - new), so values are
  // multiplied by -1
  EXPECT_NEAR(*cells.at(1).doubleValue(), -7.3, 1e-8); // goal1
  EXPECT_NEAR(*cells.at(2).doubleValue(), -0.9, 1e-8); // soft_constraint1
  EXPECT_EQ(*cells.at(3).stringValue(), "task3"); // object
  EXPECT_EQ(*cells.at(4).stringValue(), "host1"); // source container
  EXPECT_EQ(*cells.at(5).stringValue(), "host2"); // destination container
  EXPECT_GE(static_cast<int>(*cells.at(6).doubleValue()), 1); // cycle id
}

TEST_F(ModelServerTest, MoveSetsWithObjectiveNamesAndAppliedValues) {
  // for small problems, we expect to be able to display change in objectives
  // since we do apply all the moves and compute this explicitly
  auto model = getModelWithConstraintsAndGoals();
  auto result = model->getProblemMetadata();
  EXPECT_TRUE(*result.canDisplayObjChangesInMoveSetsTable());

  // Now test the MoveSets functionality with only two objective names
  auto request = TestUtils::prepareMoveSetsRequest(0, 2);
  request.objectiveNames() =
      std::vector<std::string>{"goal2", "constraint1", "constraint2"};

  const auto response = model->getMoveSets(request);
  const auto& table = *response.table();
  const auto& columns = *table.columns();
  EXPECT_EQ(
      columns.size(), 8); // 4 base columns + 3 objective columns + Cycle Id
  EXPECT_EQ(*columns.at(0).name(), "MoveSet #");
  EXPECT_EQ(*columns.at(1).name(), "goal2 (\u0394)");
  EXPECT_EQ(*columns.at(2).name(), "constraint1 (\u0394)");
  EXPECT_EQ(*columns.at(3).name(), "constraint2 (\u0394)");
  EXPECT_EQ(*columns.at(4).name(), "Object");
  EXPECT_EQ(*columns.at(5).name(), "Source Container");
  EXPECT_EQ(*columns.at(6).name(), "Destination Container");
  EXPECT_EQ(*columns.at(7).name(), "Cycle Id");

  EXPECT_EQ(*columns.at(0).type(), ColumnType::IDENTIFIER);
  EXPECT_EQ(*columns.at(1).type(), ColumnType::DOUBLE);
  EXPECT_EQ(*columns.at(2).type(), ColumnType::DOUBLE);
  EXPECT_EQ(*columns.at(3).type(), ColumnType::DOUBLE);
  EXPECT_EQ(*columns.at(4).type(), ColumnType::STRING);
  EXPECT_EQ(*columns.at(5).type(), ColumnType::STRING);
  EXPECT_EQ(*columns.at(6).type(), ColumnType::STRING);
  EXPECT_EQ(*columns.at(7).type(), ColumnType::INTEGER);

  // Verify we have rows for moves in movesets 0 and 1; we expect 5 rows (3 from
  // first moveset, 1 from second)
  EXPECT_EQ(table.rows()->size(), 4);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());

  std::map<int, std::vector<std::vector<CellData>>> rowsByMoveSet;
  for (const auto& row : *table.rows()) {
    const auto& cells = *row.cells();
    EXPECT_EQ(cells.size(), 8);
    const int moveSetNum = static_cast<int>(*cells.at(0).doubleValue());
    rowsByMoveSet[moveSetNum].push_back(cells);
  }

  // Verify we have exactly 2 movesets (1 and 2)
  EXPECT_EQ(rowsByMoveSet.size(), 2);
  EXPECT_TRUE(rowsByMoveSet.contains(1));
  EXPECT_TRUE(rowsByMoveSet.contains(2));

  // Verify moveset 1 has 3 moves
  EXPECT_EQ(rowsByMoveSet[1].size(), 3);
  for (const auto& cells : rowsByMoveSet[1]) {
    EXPECT_EQ(static_cast<int>(*cells.at(0).doubleValue()), 1);

    // Verify objective values
    EXPECT_NEAR(*cells.at(1).doubleValue(), -8, 1e-8); // goal2
    EXPECT_NEAR(*cells.at(2).doubleValue(), -3.0, 1e-8); // constraint1
    EXPECT_NEAR(*cells.at(3).doubleValue(), -3.0, 1e-8); // constraint2

    const auto& object = *cells.at(4).stringValue();
    const auto& srcContainer = *cells.at(5).stringValue();
    const auto& dstContainer = *cells.at(6).stringValue();
    EXPECT_EQ(srcContainer, "host0");
    EXPECT_EQ(dstContainer, "host1");
    EXPECT_TRUE(object == "task0" || object == "task1" || object == "task2");
  }

  // moveset 2 has 1 move
  EXPECT_EQ(rowsByMoveSet[2].size(), 1);
  const auto& cells = rowsByMoveSet[2][0];
  EXPECT_EQ(static_cast<int>(*cells.at(0).doubleValue()), 2);

  // Verify objective values
  EXPECT_NEAR(*cells.at(1).doubleValue(), -1, 1e-8); // goal2
  EXPECT_NEAR(*cells.at(2).doubleValue(), 0.0, 1e-8); // constraint1
  EXPECT_NEAR(*cells.at(3).doubleValue(), 0.0, 1e-8); // constraint2
  EXPECT_EQ(*cells.at(4).stringValue(), "task3"); // object
  EXPECT_EQ(*cells.at(5).stringValue(), "host1"); // source container
  EXPECT_EQ(*cells.at(6).stringValue(), "host2"); // destination container
}

TEST_F(ModelServerTest, MoveSetsNonIntermediateAssignments) {
  MoveSetsRequest request;
  request.assignmentA()->base() = AssignmentBase::INITIAL;
  request.assignmentB()->base() = AssignmentBase::FINAL;

  const auto table = *modelWithMovesSummary->getMoveSets(request).table();
  // Verify we have the expected columns
  const auto& columns = *table.columns();
  EXPECT_EQ(columns.size(), 5);
  EXPECT_EQ(columns.at(0).name().value(), "MoveSet #");
  EXPECT_EQ(columns.at(1).name().value(), "Object");
  EXPECT_EQ(columns.at(2).name().value(), "Source Container");
  EXPECT_EQ(columns.at(3).name().value(), "Destination Container");
  EXPECT_EQ(columns.at(4).name().value(), "Cycle Id");

  // just verify that rows w.r.t. all movesets are returned
  // because moveSets 1 and 2 have 3 moves each, and moveSet2 has 2 moves
  EXPECT_EQ(table.rows()->size(), 8);
  EXPECT_EQ(*table.totalCount(), table.rows()->size());
}

TEST_F(ModelServerTest, MoveSetsNonDisjointPartition) {
  auto request = TestUtils::prepareMoveSetsRequest(0, 1, "overlapped");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      modelWithOverlappedPartition->getMoveSets(request),
      "Expected partition to be disjoint");
}

TEST_F(ModelServerTest, DynamicDimensions) {
  // Query the host table to verify dynamic dimension columns
  OrderColumn oc;
  oc.name() = "host";
  oc.direction() = OrderDirection::ASCENDING;
  Order order;
  order.columns() = {oc};
  auto hostQuery = TestUtils::prepareQuery(std::string("host"), order);
  auto hostResult = getData(*model, hostQuery);

  // Find the dynamic dimension columns
  int srcDynamicLoadColIdx = -1;
  int dstDynamicLoadColIdx = -1;
  int hostColIdx = -1;

  for (size_t i = 0; i < hostResult.columns()->size(); ++i) {
    const auto& colName = *hostResult.columns()->at(i).name();
    if (colName == "src.dynamicLoad") {
      srcDynamicLoadColIdx = static_cast<int>(i);
    } else if (colName == "dst.dynamicLoad") {
      dstDynamicLoadColIdx = static_cast<int>(i);
    } else if (colName == "host") {
      hostColIdx = static_cast<int>(i);
    }
  }

  ASSERT_NE(srcDynamicLoadColIdx, -1) << "src.dynamicLoad column not found";
  ASSERT_NE(dstDynamicLoadColIdx, -1) << "dst.dynamicLoad column not found";
  ASSERT_NE(hostColIdx, -1) << "host column not found";

  // Verify values for each host
  std::map<std::string, std::pair<double, double>> hostDynamicLoad;
  for (const auto& row : *hostResult.rows()) {
    const auto& cells = *row.cells();
    const auto hostName = *cells.at(hostColIdx).stringValue();
    const auto srcDynamicLoad = *cells.at(srcDynamicLoadColIdx).doubleValue();
    const auto dstDynamicLoad = *cells.at(dstDynamicLoadColIdx).doubleValue();
    hostDynamicLoad[hostName] = {srcDynamicLoad, dstDynamicLoad};
  }

  // host0 is assigned to rack0 (msb0) in initial assignment, which has a
  // dynamic load of 10.
  // host0 is assigned to rack2 (msb1) in final assignment, which has a
  // default load of 1.
  EXPECT_EQ(hostDynamicLoad["host0"].first, 10); // src.dynamicLoad
  EXPECT_EQ(hostDynamicLoad["host0"].second, 1); // dst.dynamicLoad

  // All other hosts have a default value of 1 regardless of their assignment.
  EXPECT_EQ(hostDynamicLoad["host1"].first, 1);
  EXPECT_EQ(hostDynamicLoad["host1"].second, 1);
  EXPECT_EQ(hostDynamicLoad["host2"].first, 1);
  EXPECT_EQ(hostDynamicLoad["host2"].second, 1);
  EXPECT_EQ(hostDynamicLoad["host3"].first, 1);
  EXPECT_EQ(hostDynamicLoad["host3"].second, 1);
  EXPECT_EQ(hostDynamicLoad["host4"].first, 1);
  EXPECT_EQ(hostDynamicLoad["host4"].second, 1);

  // Query the dynamicLoad table to verify its structure
  OrderColumn dynamicLoadOrderCol;
  dynamicLoadOrderCol.name() = "host";
  dynamicLoadOrderCol.direction() = OrderDirection::ASCENDING;
  Order dynamicLoadOrder;
  dynamicLoadOrder.columns() = {dynamicLoadOrderCol};
  auto dynamicLoadQuery =
      TestUtils::prepareQuery(std::string("dynamicLoad"), dynamicLoadOrder);
  auto dynamicLoadResult = getData(*model, dynamicLoadQuery);

  // Verify the columns
  const auto& dynamicLoadColumns = *dynamicLoadResult.columns();
  ASSERT_EQ(dynamicLoadColumns.size(), 3);

  // Find column indices by name and verify they exist
  bool hasHostCol = false, hasMsbCol = false, hasDynamicLoadCol = false;
  for (const auto& col : dynamicLoadColumns) {
    if (*col.name() == "host") {
      hasHostCol = true;
    } else if (*col.name() == "msb") {
      hasMsbCol = true;
    } else if (*col.name() == "dynamicLoad") {
      hasDynamicLoadCol = true;
    }
  }
  EXPECT_TRUE(hasHostCol);
  EXPECT_TRUE(hasMsbCol);
  EXPECT_TRUE(hasDynamicLoadCol);
}

TEST_F(ModelServerTest, GetDataUnknownEntityThrows) {
  OrderColumn oc;
  oc.name() = "name";
  oc.direction() = OrderDirection::ASCENDING;
  Order order;
  order.columns() = {oc};
  auto query =
      TestUtils::prepareQuery(std::string("nonexistent_entity"), order);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      getData(*model, query), "Unknown entity: 'nonexistent_entity'");
}
