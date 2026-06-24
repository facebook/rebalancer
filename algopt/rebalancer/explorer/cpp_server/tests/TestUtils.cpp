// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/tests/TestUtils.h"

#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"
#include "algopt/rebalancer/interface/tests/utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/json/DynamicConverter.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

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

void TestUtils::writeToFile(
    const std::string& filename,
    const std::string& content) {
  folly::writeFileAtomic(filename, content, 0644, folly::SyncType::WITH_SYNC);
}

std::unique_ptr<ModelServer> TestUtils::buildUniverse(
    std::optional<AvoidMovingSpec> spec,
    std::optional<MovesInProgressSpec> inProgressSpec,
    bool includeMovesSummary,
    bool includeOverlappedPartition,
    bool useLocalSearchStageSolver) {
  auto bundle = buildBundle(
      {.spec = std::move(spec),
       .inProgressSpec = std::move(inProgressSpec),
       .includeMovesSummary = includeMovesSummary,
       .includeOverlappedPartition = includeOverlappedPartition,
       .useLocalSearchStageSolver = useLocalSearchStageSolver});
  return std::make_unique<ModelServer>(std::move(bundle));
}
struct MoveSummariesAndFinalAssignment {
  std::vector<MovesSummary> summaries;
  folly::F14FastMap<std::string, std::string> finalAssignment;
};
static MoveSummariesAndFinalAssignment
buildMovesSummaryWithGoalsAndConstraints() {
  std::vector<TestUtils::MoveSet> moveSets;
  {
    TestUtils::MoveSet moveSet1;
    moveSet1.moves = {
        {.object = "task0", .srcContainer = "host0", .dstContainer = "host1"},
        {.object = "task1", .srcContainer = "host0", .dstContainer = "host1"},
        {.object = "task2", .srcContainer = "host0", .dstContainer = "host1"}};
    // change here is old value - new value; Note that the values may not be
    // actual change in objective when this move happens but are just some mock
    // values
    moveSet1.objectiveNameToChange = {
        {"goal1", 8.0},
        {"goal2", 8.0},
        {"constraint1", 3.0},
        {"constraint2", 3.0},
        {"soft_constraint1", 3.7}};
    moveSets.push_back(std::move(moveSet1));
  }

  {
    TestUtils::MoveSet moveSet2;
    moveSet2.moves = {
        {.object = "task3", .srcContainer = "host1", .dstContainer = "host2"}};
    // change here is old value - new value; Note that the values may not be
    // actual change in objective when this move happens but are just some mock
    // values
    moveSet2.objectiveNameToChange = {
        {"goal1", 7.3},
        {"goal2", -4.1},
        {"constraint1", 2.8},
        {"constraint2", -1.2},
        {"soft_constraint1", 0.9}};
    moveSets.push_back(std::move(moveSet2));
  }

  {
    TestUtils::MoveSet moveSet3;
    moveSet3.moves = {
        {.object = "task0", .srcContainer = "host2", .dstContainer = "host0"}};
    // change here is old value - new value; Note that the values may not be
    // actual change in objective when this move happens but are just some mock
    // values
    moveSet3.objectiveNameToChange = {
        {"goal1", 3.2},
        {"goal2", 1.8},
        {"constraint1", -0.5},
        {"constraint2", 2.0},
        {"soft_constraint1", -1.4}};
    moveSets.push_back(std::move(moveSet3));
  }

  folly::F14FastMap<std::string, std::string> finalAssignment = {
      {"task0", "host0"},
      {"task1", "host1"},
      {"task2", "host1"},
      {"task3", "host2"},
  };

  return {
      .summaries = TestUtils::makeMovesSummaries(moveSets),
      .finalAssignment = std::move(finalAssignment)};
}

static std::vector<MovesSummary> buildMovesSummary(
    bool useLocalSearchStageSolver) {
  SingleMove move1;
  move1.object() = "host0";
  move1.srcContainer() = "rack0";
  move1.dstContainer() = "rack1";

  SingleMove move2;
  move2.object() = "host1";
  move2.srcContainer() = "rack0";
  move2.dstContainer() = "rack1";

  SingleMove move3;
  move3.object() = "host2";
  move3.srcContainer() = "rack0";
  move3.dstContainer() = "rack1";

  MovesSummary step1;
  step1.moves() = {std::move(move1), std::move(move2), std::move(move3)};
  step1.cycleId() = 1;
  if (useLocalSearchStageSolver) {
    step1.stageId() = 0;
  }

  SingleMove move4;
  move4.object() = "host0";
  move4.srcContainer() = "rack1";
  move4.dstContainer() = "rack2";

  SingleMove move5;
  move5.object() = "host3";
  move5.srcContainer() = "rack1";
  move5.dstContainer() = "rack2";

  SingleMove move6;
  move6.object() = "host2";
  move6.srcContainer() = "rack1";
  move6.dstContainer() = "rack0";

  MovesSummary step2;
  step2.moves() = {std::move(move4), std::move(move5), std::move(move6)};
  step2.cycleId() = 1;
  if (useLocalSearchStageSolver) {
    step2.stageId() = 1;
  }

  SingleMove move7;
  move7.object() = "host3";
  move7.srcContainer() = "rack2";
  move7.dstContainer() = "rack1";

  SingleMove move8;
  move8.object() = "host1";
  move8.srcContainer() = "rack1";
  move8.dstContainer() = "rack0";

  MovesSummary step3;
  step3.moves() = {std::move(move7), std::move(move8)};
  step3.cycleId() = 2;
  if (useLocalSearchStageSolver) {
    step3.stageId() = 1;
  }

  return {std::move(step1), std::move(step2), std::move(step3)};
}

std::unique_ptr<ModelServer> TestUtils::buildUniverseWithConstraintsAndGoals(
    BuildWithConstraintsAndGoalsConfig config) {
  auto bundle = buildConstraintsAndGoalsBundle(std::move(config));
  return std::make_unique<ModelServer>(std::move(bundle));
}

Bundle TestUtils::buildConstraintsAndGoalsBundle(
    BuildWithConstraintsAndGoalsConfig config) {
  // Build the problem.
  UniverseProblemBuilder builder(
      std::make_unique<AsyncConfig>(
          getTestExecutor(/*parallelExecutor=*/true)));
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3"}},
          {"host2", {}},
      });
  builder.addPartition(
      "job",
      std::map<std::string, std::vector<std::string>>({
          {"job0", {"task0", "task1"}},
          {"job1", {"task2", "task3"}},
      }));
  builder.addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{{"task0", 10}, {"task1", 20}},
      5);

  {
    auto spec = GroupCountSpec();
    spec.name() = "goal1";
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.partitionName() = "job";

    builder.addGoal(std::move(spec), /*weight=*/1.0, /*tuplePos=*/0);
  }
  {
    auto spec = GroupCountSpec();
    spec.name() = "goal2";
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.partitionName() = "job";

    builder.addGoal(std::move(spec), /*weight=*/1.0, /*tuplePos=*/0);
  }
  {
    auto spec = ToFreeSpec();
    spec.name() = "constraint1";
    spec.containers() = {"host0"};
    builder.addConstraint(
        std::move(spec),
        /*policy=*/std::nullopt,
        /*invalidCost=*/1,
        /*invalidState=*/0.0,
        /*tuplePosIfBroken=*/1);
  }
  {
    auto spec = ToFreeSpec();
    spec.name() = "constraint2";
    spec.containers() = {"host0"};
    builder.addConstraint(
        std::move(spec),
        /*policy=*/std::nullopt,
        /*invalidCost=*/1,
        /*invalidState=*/0.0,
        /*tuplePosIfBroken=*/1);
  }
  {
    auto spec = ToFreeSpec();
    spec.name() = "soft_constraint1";
    spec.containers() = {"host1"};
    builder.addConstraint(
        std::move(spec),
        /*policy=*/std::nullopt,
        /*invalidCost=*/std::nullopt,
        /*invalidState=*/std::nullopt,
        /*tuplePosIfBroken=*/1);
  }

  {
    auto spec = ToFreeSpec();
    spec.name() = "initially_satisfied_constraint1";
    spec.containers() = {"host2"};
    builder.addConstraint(
        std::move(spec),
        /*policy=*/std::nullopt,
        /*invalidCost=*/std::nullopt,
        /*invalidState=*/std::nullopt,
        /*tuplePosIfBroken=*/1);
  }

  AssignmentProblem problem;
  problem.constraintPolicy() = ConstraintPolicy::DEFAULT;
  problem.universe() = builder.build()->toThrift();
  if (config.moveStatsSpec) {
    problem.moveStatsSpec() = std::move(*config.moveStatsSpec);
  }

  const LocalSearchSolverSpec localSearchSpec;
  ThriftStrategyBuilder strategyBuilder;
  strategyBuilder.addSolver(localSearchSpec);
  problem.strategy() = strategyBuilder.build();

  AssignmentSolution solution;
  auto [summaries, finalAssignment] =
      buildMovesSummaryWithGoalsAndConstraints();
  solution.movesSummary() = std::move(summaries);
  solution.assignment() = std::move(finalAssignment);

  Bundle bundle;
  bundle.problem() = std::move(problem);
  bundle.solution() = std::move(solution);
  return bundle;
}

Bundle TestUtils::buildBundle(const BuildFilesOptions& buildOptions) {
  // Build the problem.
  UniverseProblemBuilder builder(
      std::make_unique<AsyncConfig>(
          getTestExecutor(/*parallelExecutor=*/true)));
  builder.setObjectName("host");
  builder.setContainerName("rack");
  builder.setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"rack0", {"host0", "host1", "host2"}},
          {"rack1", {"host3"}},
          {"rack2", {"host4"}},
      });
  builder.addScope(
      "row",
      std::unordered_map<std::string, std::string>{
          {"rack0", "row0"},
          {"rack1", "row1"},
          {"rack2", "row1"},
      });
  builder.addScope(
      "msb",
      std::unordered_map<std::string, std::string>{
          {"rack0", "msb0"},
          {"rack1", "msb0"},
          {"rack2", "msb1"},
      });
  builder.addScope(
      // a scope that does not cover all containers
      "row_incomplete",
      std::unordered_map<std::string, std::string>{
          {"rack0", "row0"},
          {"rack1", "row1"},
      });
  builder.addPartition(
      "scheme",
      std::map<std::string, std::vector<std::string>>({
          {"twshared", {"host0", "host3"}},
          {"cache", {"host1"}},
      }));
  if (buildOptions.includeOverlappedPartition) {
    builder.addPartition(
        "overlapped",
        std::map<std::string, std::vector<std::string>>({
            {"group1", {"host0", "host3"}},
            {"group2", {"host0", "host1"}},
        }));
  }
  builder.addObjectDimension(
      "ram",
      std::unordered_map<std::string, double>{
          {"host0", 64000}, {"host1", 32000}},
      128000);
  builder.addObjectDimension(
      "network",
      std::map<std::string, std::vector<double>>{
          {"host0", {1.5, 2.5, 9.0}},
          {"host2", {3.5, 4.5, 0}},
          {"host3", {3.0, 4.5, 1.0}},
      });

  builder.addDynamicObjectDimension(
      "dynamicLoad",
      "msb",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"msb0", {{"host0", 10}}},
      },
      1.0 /*default value*/);

  // nothing about objectPartitionRoutingDimension is displayed in explorer.
  // Just adding this to verify that there are no exceptions
  builder.addRoutingConfig("dummyRoutingConfig", "rack", "scheme", {}, {}, {});
  builder.addObjectPartitionRoutingDimension(
      "routingDimension", "scheme", "dummyRoutingConfig", {});

  builder.addContainerDimension(
      "network",
      std::unordered_map<std::string, double>{
          {"rack0", 100.0},
          {"rack1", 200.0},
      });
  builder.addScopeDimension(
      "network",
      "msb",
      std::unordered_map<std::string, double>{
          {"msb0", 1000.0},
          {"msb1", 0},
      });

  if (auto spec = buildOptions.spec) {
    builder.addConstraint(
        std::move(*spec),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt);
  }

  if (auto inProgressSpec = buildOptions.inProgressSpec) {
    builder.addConstraint(
        std::move(*inProgressSpec),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt);
  }

  // final assignment
  folly::F14FastMap<std::string, std::string> finalAssignment = {
      {"host0", "rack2"},
      {"host1", "rack0"},
      {"host2", "rack0"},
      {"host3", "rack1"},
      {"host4", "rack2"},
  };

  // load the problem
  AssignmentProblem problem;
  problem.constraintPolicy() = ConstraintPolicy::DEFAULT;
  problem.universe() = builder.build()->toThrift();

  ThriftStrategyBuilder strategyBuilder;
  if (buildOptions.useLocalSearchStageSolver) {
    const LocalSearchStageSolverSpec localSearchStageSolverSpec;
    strategyBuilder.addSolver(localSearchStageSolverSpec);
  } else {
    const LocalSearchSolverSpec localSearchSpec;
    strategyBuilder.addSolver(localSearchSpec);
  }
  problem.strategy() = strategyBuilder.build();

  // Create bundle.
  Bundle bundle;
  bundle.problem() = std::move(problem);

  if (buildOptions.includeSolutionObject) {
    AssignmentSolution solution;
    solution.assignment() = std::move(finalAssignment);
    if (buildOptions.includeMovesSummary) {
      solution.movesSummary() =
          buildMovesSummary(buildOptions.useLocalSearchStageSolver);
    }

    bundle.solution() = std::move(solution);
  }

  return bundle;
}

Bundle TestUtils::buildEvaluatorProblem() {
  auto solver = ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");

  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3", "task4"}},
          {"host2", {"task5"}},
          {"host3", {}},
      });
  solver->addPartition(
      "job",
      std::map<std::string, std::vector<std::string>>({
          {"job0", {"task0", "task1"}},
          {"job1", {"task2", "task3"}},
          {"job2", {"task4", "task5"}},
      }));

  solver->addScope(
      "rack",
      std::map<std::string, std::string>{
          {"host0", "rack0"},
          {"host1", "rack0"},
          {"host2", "rack1"},
          {"host3", "rack1"},
      });
  solver->addObjectDimension(
      "cpu",
      std::vector<std::pair<std::string, double>>{
          {"task0", 10},
          {"task1", 20},
          {"task2", 30},
          {"task3", 40},
          {"task4", 50},
          {"task5", 60}});
  solver->addObjectDimension(
      "network",
      folly::F14FastMap<std::string, double>{
          {"task0", 600},
          {"task1", 200},
          {"task2", 400},
          {"task3", 100},
          {"task4", 500},
          {"task5", 300}});
  solver->addScopeDimension(
      "cpu",
      "host",
      folly::F14FastMap<std::string, double>{
          {"host0", 100},
          {"host1", 100},
          {"host2", 100},
          {"host3", 100},
      });
  solver->addScopeDimension(
      "network",
      "rack",
      std::vector<std::pair<std::string, double>>{
          {"rack0", 1000},
          {"rack1", 1000},
      });

  MinimizeMovementSpec specGoal;
  specGoal.name() = "minimize movement";
  specGoal.scope() = "host";
  specGoal.dimension() = "task_count";
  solver->addGoal(std::move(specGoal), 1);

  AssignmentAffinity spec;
  spec.objectName() = "task1";
  spec.scopeItemName() = "host3";
  spec.affinity() = 1;
  std::vector<AssignmentAffinity> specs;
  specs.push_back(spec);
  AssignmentAffinitiesSpec assignSpec;
  assignSpec.affinities() = std::move(specs);
  assignSpec.name() = "affinity goal";
  assignSpec.scope() = "host";
  solver->addGoal(std::move(assignSpec), 10);

  std::vector<std::string> avoidMove = {"task0"};
  AvoidMovingSpec avoidSpec;
  avoidSpec.objects() = std::move(avoidMove);
  avoidSpec.name() = "avoid moving task0";
  solver->addConstraint(avoidSpec);

  ToFreeSpec toFreeSpec;
  toFreeSpec.name() = "free host0";
  toFreeSpec.containers() = {"host0"};
  solver->addConstraint(
      toFreeSpec, std::nullopt, std::nullopt, std::nullopt, 2);

  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
  solver->addSolver(solverSpec);

  solver->solve();

  return solver->getBundle();
}

Query TestUtils::prepareQuery(
    const std::string& tableName,
    const std::vector<FilterRule>& rules) {
  Filter filter;
  filter.rules() = rules;
  Query query;
  query.entity() = tableName;
  query.filter() = std::move(filter);
  return query;
}

Query TestUtils::prepareQuery(
    const std::string& tableName,
    const Group& group,
    std::optional<std::vector<FilterRule>> rules) {
  Query query;
  query.entity() = tableName;
  query.group() = group;
  if (rules != std::nullopt) {
    Filter filter;
    filter.rules() = std::move(*rules);
    query.filter() = std::move(filter);
  }

  return query;
}

Query TestUtils::prepareQuery(
    const std::string& tableName,
    const Order& Order,
    std::optional<Page> page) {
  Query query;
  query.entity() = tableName;
  query.order() = Order;
  if (page != std::nullopt) {
    query.page() = *page;
  }
  return query;
}

TypeaheadRequest TestUtils::prepareTypeheadRequest(
    const std::string& entity,
    const std::string& query,
    const int limit) {
  TypeaheadRequest request;
  request.entity() = entity;
  request.limit() = limit;
  request.query() = query;
  return request;
}

Assignment TestUtils::prepareAssigmentRequest(
    std::map<std::string, std::string> variableToContainerOverride,
    AssignmentBase base) {
  Assignment request;
  request.base() = std::move(base);
  request.variableToContainerOverride() =
      std::move(variableToContainerOverride);
  return request;
}

MovesBetweenAssignmentsRequest TestUtils::prepareMoveBetweenAssigmentRequest(
    Assignment source,
    Assignment destination) {
  MovesBetweenAssignmentsRequest request;
  request.source() = std::move(source);
  request.destination() = std::move(destination);
  return request;
}

MoveSetsRequest TestUtils::prepareMoveSetsRequest(
    int64_t startMoveSetIdx,
    int64_t endMoveSetIdx,
    std::optional<std::string> partitionName,
    std::optional<std::string> scopeName) {
  Assignment assignmentA;
  assignmentA.base() = AssignmentBase::INTERMEDIATE;
  assignmentA.searchStep() = startMoveSetIdx;
  assignmentA.variableToContainerOverride() = {};
  Assignment assignmentB;
  assignmentB.base() = AssignmentBase::INTERMEDIATE;
  assignmentB.searchStep() = endMoveSetIdx;
  assignmentB.variableToContainerOverride() = {};

  MoveSetsRequest request;
  request.assignmentA() = std::move(assignmentA);
  request.assignmentB() = std::move(assignmentB);
  if (partitionName.has_value()) {
    request.partitionName() = std::move(*partitionName);
  }
  if (scopeName.has_value()) {
    request.scopeName() = std::move(*scopeName);
  }

  // Create a basic query
  Query query;
  query.entity() = "move_sets";
  request.query() = std::move(query);

  return request;
}

std::vector<MovesSummary> TestUtils::makeMovesSummaries(
    const std::vector<MoveSet>& moveSets) {
  std::vector<interface::MovesSummary> moveSummaries;
  for (const auto& moveSet : moveSets) {
    interface::MovesSummary movesSummary;
    auto& moves = *movesSummary.moves();
    for (const auto& move : moveSet.moves) {
      SingleMove moveT;
      moveT.object() = move.object;
      moveT.srcContainer() = move.srcContainer;
      moveT.dstContainer() = move.dstContainer;
      moves.push_back(std::move(moveT));
    }
    for (const auto& [objName, change] : moveSet.objectiveNameToChange) {
      (*movesSummary.objectives())[objName].change() = change;
    }
    movesSummary.cycleId() = 1;
    moveSummaries.push_back(std::move(movesSummary));
  }
  return moveSummaries;
}
