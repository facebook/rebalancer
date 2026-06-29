// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/cpp_server/tests/TestUtils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::explorer;

const ExpressionResult& getExpression(
    const std::vector<ExpressionResult>& expressions,
    const std::string& name,
    ExpressionType type) {
  auto it = std::find_if(
      expressions.begin(),
      expressions.end(),
      [&name, &type](const ExpressionResult& expr) {
        return *expr.name() == name && *expr.type() == type;
      });
  if (it == expressions.end()) {
    throw std::runtime_error(
        fmt::format(
            "Expression with name '{}' and type '{}' not found",
            name,
            apache::thrift::util::enumNameSafe(type)));
  }
  return *it;
}

template <class T>
std::set<T> makeSet(std::initializer_list<T> items) {
  return std::set<T>(items);
}

template <class T>
std::set<T> makeSet(const std::vector<T>& items) {
  return std::set<T>(items.begin(), items.end());
}

CellData makeCellData(double value) {
  CellData cell;
  cell.doubleValue() = value;
  return cell;
}

CellData makeCellData(const std::string& value) {
  CellData cell;
  cell.stringValue() = value;
  return cell;
}

std::string toStr(const CellData& cell) {
  if (cell.doubleValue().has_value()) {
    return folly::to<std::string>(*cell.doubleValue());
  } else if (cell.stringValue().has_value()) {
    return *cell.stringValue();
  } else {
    return "";
  }
}

std::string toStr(const std::vector<CellData>& row) {
  std::string result;
  for (const auto& cell : row) {
    result.append(fmt::format("{}, ", toStr(cell)));
  }
  return result;
}

std::string toStr(const std::vector<std::vector<CellData>>& rows) {
  std::string result;
  for (const auto& row : rows) {
    result.append(fmt::format("\t{}\n", toStr(row)));
  }
  return result;
}

class ExplorerEvaluatorServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto bundle = TestUtils::buildEvaluatorProblem();
    model_ = std::make_unique<ModelServer>(std::move(bundle));
  }
  std::unique_ptr<ModelServer> model_;
};

TEST_F(ExplorerEvaluatorServerTest, EvaluateInitial) {
  Assignment assignment;
  assignment.base() = AssignmentBase::INITIAL;
  auto result = model_->evaluate(assignment);

  EXPECT_EQ(5, result.expressions()->size());

  const auto& expressions = *result.expressions();

  {
    const auto& expr =
        getExpression(expressions, "free host0", ExpressionType::CONSTRAINT);
    EXPECT_EQ(
        "toFreeSpec: To free 1 containers; 'task_count' dimension; MINIMIZE_TOTAL_UTILIZATION formula",
        *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.0, *expr.value());
  }

  {
    const auto& expr = getExpression(
        expressions, "avoid moving task0", ExpressionType::CONSTRAINT);
    EXPECT_EQ("avoidMovingSpec: Avoid moving 1 objects", *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.0, *expr.value());
  }

  {
    // softended constraint
    const auto& expr =
        getExpression(expressions, "free host0", ExpressionType::OBJECTIVE);
    EXPECT_EQ(
        "initially broken toFreeSpec: To free 1 containers; 'task_count' dimension; MINIMIZE_TOTAL_UTILIZATION formula",
        *expr.description());
    EXPECT_EQ(2, *expr.tupleIndex());
    EXPECT_EQ(10300.0, *expr.value());
  }

  {
    const auto& expr =
        getExpression(expressions, "affinity goal", ExpressionType::OBJECTIVE);
    EXPECT_EQ(
        "10 * (assignmentAffinitiesSpec: Assignment affinities of task to host)",
        *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.0, *expr.value());
  }

  {
    const auto& expr = getExpression(
        expressions, "minimize movement", ExpressionType::OBJECTIVE);
    EXPECT_EQ(
        "minimizeMovementSpec: Minimize movement on task_count for scope host",
        *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.0, *expr.value());
  }
}

TEST_F(ExplorerEvaluatorServerTest, EvaluateFinal) {
  Assignment assignment;
  assignment.base() = AssignmentBase::FINAL;
  auto result = model_->evaluate(assignment);

  EXPECT_EQ(5, result.expressions()->size());

  const auto& expressions = *result.expressions();

  {
    const auto& expr =
        getExpression(expressions, "free host0", ExpressionType::CONSTRAINT);
    EXPECT_EQ(
        "toFreeSpec: To free 1 containers; 'task_count' dimension; MINIMIZE_TOTAL_UTILIZATION formula",
        *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.0, *expr.value());
  }

  {
    const auto& expr = getExpression(
        expressions, "avoid moving task0", ExpressionType::CONSTRAINT);
    EXPECT_EQ("avoidMovingSpec: Avoid moving 1 objects", *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.0, *expr.value());
  }

  {
    // softended constraint
    const auto& expr =
        getExpression(expressions, "free host0", ExpressionType::OBJECTIVE);
    EXPECT_EQ(
        "initially broken toFreeSpec: To free 1 containers; 'task_count' dimension; MINIMIZE_TOTAL_UTILIZATION formula",
        *expr.description());
    EXPECT_EQ(2, *expr.tupleIndex());
    EXPECT_EQ(10200.0, *expr.value());
  }

  {
    const auto& expr =
        getExpression(expressions, "affinity goal", ExpressionType::OBJECTIVE);
    EXPECT_EQ(
        "10 * (assignmentAffinitiesSpec: Assignment affinities of task to host)",
        *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(-10.0, *expr.value());
  }

  {
    const auto& expr = getExpression(
        expressions, "minimize movement", ExpressionType::OBJECTIVE);
    EXPECT_EQ(
        "minimizeMovementSpec: Minimize movement on task_count for scope host",
        *expr.description());
    EXPECT_EQ(0, *expr.tupleIndex());
    EXPECT_EQ(0.00025, *expr.value());
  }
}

TEST_F(ExplorerEvaluatorServerTest, GoalSpec) {
  Assignment assignment;
  assignment.base() = AssignmentBase::INITIAL;
  auto evaluation = model_->evaluate(assignment);

  auto result = model_->getGoalSpec("affinity goal");
  EXPECT_EQ("affinity goal", result.name());
  EXPECT_EQ(10, *result.weight());
  EXPECT_EQ(0, *result.tupleIndex());
  EXPECT_EQ(
      "{\n  \"assignmentAffinitiesSpec\": {\n    \"affinities\": [\n      {\n        \"affinity\": 1,\n        \"objectName\": \"task1\",\n        \"scopeItemName\": \"host3\"\n      }\n    ],\n    \"name\": \"affinity goal\",\n    \"scope\": \"host\"\n  }\n}",
      *result.specJson());
}

TEST_F(ExplorerEvaluatorServerTest, ConstraintSpec) {
  Assignment assignment;
  assignment.base() = AssignmentBase::INITIAL;
  auto evaluation = model_->evaluate(assignment);

  auto result = model_->getConstraintSpec("avoid moving task0");
  EXPECT_EQ("avoid moving task0", result.name());
  EXPECT_EQ(100, *result.invalidCost());
  EXPECT_EQ(10000, *result.invalidState());
  EXPECT_EQ(
      facebook::rebalancer::interface::ConstraintPolicy::DEFAULT,
      result.policy());
  EXPECT_EQ(
      "{\n  \"avoidMovingSpec\": {\n    \"name\": \"avoid moving task0\",\n    \"objects\": [\n      \"task0\"\n    ]\n  }\n}",
      *result.specJson());
}

TEST_F(ExplorerEvaluatorServerTest, GetTreeNode) {
  Assignment assignment;
  assignment.base() = AssignmentBase::INITIAL;
  auto evaluation = model_->evaluate(assignment);

  const auto& expressions = *evaluation.expressions();
  const auto& minimizeMovementExpr = getExpression(
      expressions, "minimize movement", ExpressionType::OBJECTIVE);
  const auto expId = *minimizeMovementExpr.id();

  Page page;
  page.offset() = 0;
  page.limit() = 2;
  TreeNodeRequest request;
  request.expressionId() = expId;
  request.childrenPage() = page;
  request.childrenOrderDirection() = OrderDirection::ASCENDING;
  request.childrenOrderMetric() = TreeNodeOrderMetric::SOURCE_VALUE;
  request.destinationAssignment()->base() = AssignmentBase::FINAL;

  auto result = model_->getTreeNode(request);

  EXPECT_EQ(expId, *result.node()->expressionId());
  EXPECT_EQ("LinearSum", *result.node()->expressionType());
  EXPECT_EQ("", *result.node()->name());
  EXPECT_EQ(
      "minimizeMovementSpec: Minimize movement on task_count for scope host",
      *result.node()->description());
  EXPECT_EQ(0.0, *result.node()->sourceValue());
  EXPECT_EQ(0.00025, *result.node()->destinationValue());
  EXPECT_EQ(1.0, *result.node()->coefficient());

  const auto& properties = *result.node()->properties()->properties();
  EXPECT_EQ(3, properties.size());
  EXPECT_EQ(0.0, *properties.at("constant").valueDouble()->value());
  EXPECT_EQ(-0.0015, *properties.at("lower bound").valueDouble()->value());
  EXPECT_EQ(0.0075, *properties.at("upper bound").valueDouble()->value());

  EXPECT_EQ(2, result.children()->size());
  EXPECT_EQ("ObjectLookup", *result.children()->at(0).expressionType());
  EXPECT_EQ("", *result.children()->at(0).description());
  EXPECT_EQ(3.0, *result.children()->at(0).sourceValue());
  EXPECT_EQ(2.0, *result.children()->at(0).destinationValue());
  EXPECT_EQ(-0.00025, *result.children()->at(0).coefficient());
  const auto& child0Properties =
      *result.children()->at(0).properties()->properties();
  EXPECT_EQ(3, child0Properties.size());
  EXPECT_EQ(
      std::vector<std::string>({"host0"}),
      *child0Properties.at("containers").valueContainerNameList()->value());
  EXPECT_EQ(0.0, *child0Properties.at("lower bound").valueDouble()->value());
  EXPECT_EQ(3.0, *child0Properties.at("upper bound").valueDouble()->value());

  EXPECT_EQ("ObjectLookup", *result.children()->at(1).expressionType());
  EXPECT_EQ("", *result.children()->at(1).description());
  EXPECT_EQ(2.0, *result.children()->at(1).sourceValue());
  EXPECT_EQ(2.0, *result.children()->at(1).destinationValue());
  EXPECT_EQ(-0.00025, *result.children()->at(1).coefficient());
  const auto& child1Properties =
      *result.children()->at(1).properties()->properties();
  EXPECT_EQ(3, child1Properties.size());
  EXPECT_EQ(
      std::vector<std::string>({"host1"}),
      *child1Properties.at("containers").valueContainerNameList()->value());
  EXPECT_EQ(0.0, *child1Properties.at("lower bound").valueDouble()->value());
  EXPECT_EQ(2.0, *child1Properties.at("upper bound").valueDouble()->value());
}

TEST_F(ExplorerEvaluatorServerTest, MetricsTest) {
  auto metadata = model_->getProblemMetadata();
  EXPECT_EQ(
      std::vector<std::string>{"scope item utilization values"},
      *metadata.metricCollectionNames());
}

TEST_F(ExplorerEvaluatorServerTest, EvaluateMetricCollectionTest) {
  // Create a query for the metric collection
  auto query = TestUtils::prepareQuery(
      std::string("scope item utilization values"), std::vector<FilterRule>{});

  Assignment initialAssignment;
  initialAssignment.base() = AssignmentBase::INITIAL;

  Assignment finalAssignment;
  finalAssignment.base() = AssignmentBase::FINAL;

  auto result = model_->evaluateMetricCollection(
      query, initialAssignment, finalAssignment);

  // Verify that we got a valid result with columns and rows
  EXPECT_GT(result.columns()->size(), 0);

  // Verify the total count matches the number of rows
  EXPECT_EQ(*result.totalCount(), result.rows()->size());

  // Expected column names for scope item utilization values
  const std::vector<std::string> expectedColumnNames = {
      "Util Metric",
      "Dimension",
      "Scope",
      "Scope Item",
      "Partition",
      "Group",
      "Scope Item Dimension Value",
      "Relative Utilization (A)",
      "Relative Utilization (B)",
      "Relative Utilization (B-A)",
      "Utilization (A)",
      "Utilization (B)",
      "Utilization (B-A)",
  };
  std::vector<std::string> actualColumnNames;
  for (const auto& column : *result.columns()) {
    actualColumnNames.emplace_back(*column.name());
  }
  EXPECT_EQ(expectedColumnNames, actualColumnNames);

  auto makeRow = [](const std::string& metric,
                    const std::string& dimension,
                    const std::string& scope,
                    const std::string& scopeItem,
                    const std::string& partition,
                    const std::string& group,
                    double valueA,
                    double valueB,
                    double valueB_A) {
    return std::vector<CellData>{
        makeCellData(metric),
        makeCellData(dimension),
        makeCellData(scope),
        makeCellData(scopeItem),
        makeCellData(partition),
        makeCellData(group),
        makeCellData(1.0), // scope item dimension value
        makeCellData(valueA), // relative util same as absolute util
        makeCellData(valueB), // relative util same as absolute util
        makeCellData(valueB_A), // relative util same as absolute util
        makeCellData(valueA),
        makeCellData(valueB),
        makeCellData(valueB_A)};
  };

  std::vector<std::vector<CellData>> expectedRows = {
      makeRow(
          "new", "task_count", "host", "host2", "N/A", "N/A", 0.0, 0.0, 0.0),
      makeRow(
          "new", "task_count", "host", "host3", "N/A", "N/A", 0.0, 1.0, 1.0),
      makeRow(
          "new", "task_count", "host", "host1", "N/A", "N/A", 0.0, 0.0, 0.0),
      makeRow(
          "new", "task_count", "host", "host0", "N/A", "N/A", 0.0, 0.0, 0.0),
      makeRow(
          "after", "task_count", "host", "host0", "N/A", "N/A", 3.0, 2.0, -1.0),
  };

  // verify that cells are as expected
  auto& rows = *result.rows();
  EXPECT_EQ(expectedRows.size(), rows.size());

  std::string ss;
  int foundRowCount = 0;
  for (auto& row : rows) {
    ss.append(fmt::format("\t{}\n", toStr(*row.cells())));
    if (std::find(expectedRows.begin(), expectedRows.end(), *row.cells()) !=
        expectedRows.end()) {
      foundRowCount++;
    }
  }

  EXPECT_EQ(expectedRows.size(), foundRowCount)
      << "Expected to see the following rows:\n"
      << toStr(expectedRows) << "\nbut found the following:\n"
      << ss;
}
