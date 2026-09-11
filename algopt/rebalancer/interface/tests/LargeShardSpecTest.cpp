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

#include "algopt/rebalancer/interface/tests/utils.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

using namespace facebook::rebalancer::interface;

class LargeShardSpecTest : public ::testing::TestWithParam<int> {
 protected:
  void setUpBasicProblem(
      std::map<std::string, std::vector<std::string>> initialAssignment =
          defaultInitialAssignment_) {
    solver_ = initializeTestProblemSolver({.executorThreadCount = GetParam()});

    solver_->setObjectName("task");
    solver_->setContainerName("host");

    solver_->setAssignment(initialAssignment);

    taskToMemory_ = {
        {"task0", 1},
        {"task1", 9},
        {"task2", 2},
        {"task3", 4},
        {"task4", 2},
        {"task5", 6},
        {"task6", 3},
        {"task7", 10},
        {"task8", 7},
        {"task9", 0.5},
    };

    solver_->addObjectDimension("memory", taskToMemory_);

    scopeItemToMemory_ = {
        {"host0", 1000},
        {"host1", 12},
        {"host2", 13},
        {"host3", 17},
    };
    solver_->addScopeDimension("memory", "host", scopeItemToMemory_);

    folly::F14FastMap<std::string, std::vector<std::string>>
        scopeItemNameToContainers;
    scopeItemNameToContainers["hosts"] = {"host0", "host1", "host2", "host3"};

    solver_->addScope("hosts", scopeItemNameToContainers);

    facebook::rebalancer::interface::ScopeItemList scopeItems;
    scopeItems.scopeName() = "hosts";

    facebook::rebalancer::interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.defaultScopeItems() = scopeItems;

    facebook::rebalancer::interface::SingleRandomStratifiedMoveTypeSpec
        singleRandomStratifiedMoveTypeSpec;
    singleRandomStratifiedMoveTypeSpec.destinationsToExplore()
        ->set_moveToScopeItems() = moveToScopeItemsSpec;
    singleRandomStratifiedMoveTypeSpec.stratifiedSampleSize()
        ->defaultSampleSize() = 10;
    singleRandomStratifiedMoveTypeSpec.minObjectsToExplore() = 10;

    LocalSearchSolverSpec spec;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(singleRandomStratifiedMoveTypeSpec));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    spec.minHotObjects() = 10;
    solver_->addSolver(spec);
  }

  void addCapacityConstraint(
      std::string scope = "host",
      std::string dimension = "memory",
      std::map<std::string, double> scopeItemToLimit = {},
      CapacitySpecDefinition definition = CapacitySpecDefinition::AFTER) {
    // add a capacityConstraint
    CapacitySpec capacitySpec;
    capacitySpec.scope() = std::move(scope);
    capacitySpec.dimension() = std::move(dimension);
    capacitySpec.definition() = definition;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.scopeItemLimits() = std::move(scopeItemToLimit);
    limit.globalLimit() = 0;
    capacitySpec.limit() = limit;

    solver_->addConstraint(capacitySpec);
  }

  void addAvoidMovingConstraint(std::vector<std::string> objects) {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = std::move(objects);

    solver_->addConstraint(avoidMovingSpec);
  }

  void addToFreeConstraint(std::vector<std::string> containers) {
    ToFreeSpec toFreeSpec;
    toFreeSpec.containers() = std::move(containers);

    solver_->addConstraint(toFreeSpec);
  }

  void addMovesInProgressConstraint(std::map<std::string, std::string> moves) {
    MovesInProgressSpec movesSpec;

    std::vector<MoveInProgress> movesInProgress;

    for (auto& [obj, container] : moves) {
      MoveInProgress move;
      move.objName() = obj;
      move.toContainer() = container;

      movesInProgress.push_back(std::move(move));
    }

    movesSpec.moves() = std::move(movesInProgress);

    solver_->addConstraint(movesSpec);
  }

  void addNonAcceptingConstraint(std::vector<std::string> containers) {
    NonAcceptingSpec nonAcceptingSpec;
    nonAcceptingSpec.scope() = "host";
    nonAcceptingSpec.items() = std::move(containers);

    solver_->addConstraint(nonAcceptingSpec);
  }

  void addLargeShardGoal(
      std::string scope = "host",
      std::string dimension = "memory",
      std::string unassignedScopeItemName = "host0",
      std::vector<std::string> blockList = {},
      int weight = 1) {
    // add a largeShard goal
    LargeShardSpec spec;
    spec.scope() = std::move(scope);
    spec.unassignedScopeItemName() = std::move(unassignedScopeItemName);
    spec.dimension() = std::move(dimension);

    Filter filter;
    filter.itemsBlacklist() = std::move(blockList);
    spec.filter() = std::move(filter);

    // add the goal at tuple index 1 with weight 'weight'
    solver_->addGoal(spec, weight, 1);
  }

  std::unique_ptr<ProblemSolver> solver_;
  std::map<std::string, double> taskToMemory_;
  std::map<std::string, double> scopeItemToMemory_;
  std::map<std::string, double> scopeItemToBandwidth_;

  static std::map<std::string, std::vector<std::string>>
      defaultInitialAssignment_;
};

std::map<std::string, std::vector<std::string>>
    LargeShardSpecTest::defaultInitialAssignment_ = {
        {"host0", {"task7", "task0", "task1"}},
        {"host1", {"task2", "task6"}},
        {"host2", {"task3", "task5"}},
        {"host3", {"task8", "task4", "task9"}},
};

INSTANTIATE_TEST_CASE_P(NumThreads, LargeShardSpecTest, testThreadCounts());

TEST_P(LargeShardSpecTest, BasicGoalWithDrain) {
  setUpBasicProblem();

  addLargeShardGoal();

  addToFreeConstraint({"host0"});
  addCapacityConstraint("host", "memory", scopeItemToMemory_);
  addAvoidMovingConstraint({"task0", "task1", "task8"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, one can see that we expect "host3"
  // to be the candidateContainer since it has the smallest DrainMetric (in this
  // case the smallest 'maxSizeShardToRemoved')

  // We expect the initial objective value at tuple index 1 to be
  // (largeShardSize - currAvailableCapacity in host3) = (10 - (17 - 9.5)) = 2.5
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 2.5, 1e-8);

  // Expect final value for goal at tuple index 1 to be zero since we expect
  // the largeShard to move into host3
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to move into host3, and tasks4 and 9 to have moved out
  auto finalAssignment = *solution.assignment();
  EXPECT_EQ(finalAssignment.at("task7"), "host3");
  EXPECT_NE(finalAssignment.at("task4"), "host3");
  EXPECT_NE(finalAssignment.at("task9"), "host3");
}

TEST_P(LargeShardSpecTest, BasicGoalWithFilterAndWeight) {
  setUpBasicProblem();

  const std::vector<std::string> blockScopeItems = {"host1", "host3"};
  addLargeShardGoal("host", "memory", "host0", blockScopeItems, 2 /* weight */);

  addToFreeConstraint({"host0"});
  addCapacityConstraint("host", "memory", scopeItemToMemory_);
  addAvoidMovingConstraint({"task0", "task1", "task8"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, one can see that in this example,
  // we expect "host1" to be the candidateContainer since host1 and host are
  // part of filter.itemsBlacklist()

  // We expect the initial objective value at tuple index 1 to be
  // 2*(largeShardSize - currAvailableCapacity in host2) = 2*(10 - (13 - 10)) =
  // 14
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 14.0, 1e-8);

  // Expect final value for goal at tuple index 1 to be zero since we expect
  // the largeShard to move into host2
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to move into host2, and tasks 3 and 5 to have moved out
  auto finalAssignment = *solution.assignment();
  EXPECT_EQ(finalAssignment.at("task7"), "host2");
  EXPECT_NE(finalAssignment.at("task3"), "host2");
  EXPECT_NE(finalAssignment.at("task5"), "host2");
}

TEST_P(LargeShardSpecTest, ConstraintViolationForAContainer) {
  setUpBasicProblem();

  addLargeShardGoal();

  addToFreeConstraint({"host0"});
  addCapacityConstraint("host", "memory", scopeItemToMemory_);
  addAvoidMovingConstraint({"task0", "task1", "task4"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, one can see that we expect "host1"
  // to be the candidateContainer since it has the smallest DrainMetric (in this
  // case the smallest 'maxSizeShardToRemoved'). This is different from the
  // example in 'BasicGoalWithDrain', since here task4 in host3 is not allowed
  // to move and so its maxSizeShardToRemoved increases (to 7).

  // We expect the initial objective value at tuple index 1 to be
  // (largeShardSize - currAvailableCapacity in host1) = (10 - (12 - 5)) = 3
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 3.0, 1e-8);

  // Expect final value for goal at tuple index 1 to be 0 since we expect task7
  // to move to host1
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to move to host1
  auto finalAssignment = *solution.assignment();
  EXPECT_EQ(finalAssignment.at("task7"), "host1");
}

TEST_P(LargeShardSpecTest, NoCandidateContainerFound) {
  setUpBasicProblem();

  addLargeShardGoal();

  addToFreeConstraint({"host0"});
  addCapacityConstraint("host", "memory", scopeItemToMemory_);
  addAvoidMovingConstraint(
      {"task0", "task1", "task2", "task3", "task4", "task5", "task6", "task8"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, one can see that there is no
  // candidateContainer since only currently assigned object that can be moved
  // is task9, but that's not enough to accommodate task7

  // We expect the initial objective value at tuple index 1 to be 10, since
  // there is no candidateContainer and size of task7 is 10
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 10.0, 1e-8);

  // Expect final value for goal at tuple index 1 to be unchanged since the
  // only currently assigned object that can be moved is task9, but that's not
  // enough to accommodate task7
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 10.0);

  // We expect task7 to remain in host0
  auto finalAssignment = *solution.assignment();
  EXPECT_EQ(finalAssignment.at("task7"), "host0");

  // Also, notice the Warning in the logs
}

TEST_P(LargeShardSpecTest, MultiRoundWithDuringAndAfterCapacity) {
  // This test is to demonstrate how LargeShardSpec is expected to work with a
  // DURING_AND_AFTER capacity definition. In particular, here there are two
  // rounds of allocations and the only difference between them is the starting
  // initialAssignment. In round 1, we start with the defaultInitialAssignment_
  // like in all the other examples here. Due to the DURING_AND_AFTER capacity
  // definition, the solver can only do enough to make space for the
  // candidateLargeShard (task7) in the candidateContainer (host3), but CANNOT
  // move the shard into it. Hence, to ensure that task7 gets allocated as
  // expected, we need a second round where the initialAssignment is the same
  // as the finalAssignment from round 1.
  auto setUpGoalsAndConstraints = [&]() {
    addLargeShardGoal();

    addToFreeConstraint({"host0"});
    addCapacityConstraint(
        "host",
        "memory",
        scopeItemToMemory_,
        CapacitySpecDefinition::DURING_AND_AFTER);
    addAvoidMovingConstraint({"task0", "task1", "task8"});
  };

  //=================================
  //      ROUND 1
  //===================================

  setUpBasicProblem();

  setUpGoalsAndConstraints();

  auto round1Solution = solver_->solve();
  auto round1InitialObjectives =
      round1Solution.initialGlobalObjective()->goals();
  auto round1FinalObjectives = round1Solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, round1FinalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, one can see that we expect "host3"
  // to be the candidateContainer since it has the smallest DrainMetric.

  // We expect the initial objective value at tuple index 1 to be
  // (largeShardSize - currAvailableCapacity in host3) = (10 - (17 - 9.5)) = 2.5
  auto initialObjValue = round1InitialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 2.5, 1e-8);

  // Expect final value for goal at tuple index 1 to be zero since we expect
  // tasks 4 and 9 to have moved out of host3 to make way for task7. However,
  // note that because of the DURING_AND_AFTER_CAPACITY definition, task7 CANNOT
  // move into host3.
  auto finalObjValue = round1FinalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to STAY at host0, and tasks4 and 9 to have moved out
  auto round1FinalAssignment = *round1Solution.assignment();
  EXPECT_EQ(round1FinalAssignment.at("task7"), "host0");
  EXPECT_NE(round1FinalAssignment.at("task4"), "host3");
  EXPECT_NE(round1FinalAssignment.at("task9"), "host3");

  //=================================
  //     ROUND 2
  //===================================

  // Use the final assignment of round 1 as initialAssignment in round 2
  std::map<std::string, std::vector<std::string>> round2InitialAssignment;
  for (auto& [task, host] : round1FinalAssignment) {
    round2InitialAssignment[host].push_back(task);
  }
  setUpBasicProblem(round2InitialAssignment);

  setUpGoalsAndConstraints();

  auto round2Solution = solver_->solve();

  // We expect task7 to now have moved in to host3
  auto round2FinalAssignment = *round2Solution.assignment();
  EXPECT_EQ(round2FinalAssignment.at("task7"), "host3");
}

TEST_P(LargeShardSpecTest, BasicGoalWithMovesInPogressSpec) {
  setUpBasicProblem();

  addLargeShardGoal();

  addToFreeConstraint({"host0"});
  addCapacityConstraint("host", "memory", scopeItemToMemory_);
  addMovesInProgressConstraint({{"task4", "host1"}, {"task9", "host2"}});
  addAvoidMovingConstraint({"task0", "task1", "task4", "task9", "task8"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, one can see that we expect "host3"
  // to be the candidateContainer. Although all the tasks in host3 are part of
  // avoidMoving spec, tasks 4 and 9 are also listed in a MovesInProgressSpec.
  // And since it is enough for these to move in order for task7 to fit, host3
  // is the candidateContainer.

  // We expect the initial objective value at tuple index 1 to be 0. This is
  // because, since there is movesInProgressSpec, the initialAssignment is
  // updated to acount for those moves. Since moving "task4" and "task9" results
  // in enough empty space in host3 to accommodate task7, the objective value
  // will be 0
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 0.0, 1e-8);

  // Expect final value for goal at tuple index 1 to be zero since we expect
  // the largeShard to move into host3
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to move into host3, and tasks4 and 9 to have moved out
  auto finalAssignment = *solution.assignment();
  EXPECT_EQ(finalAssignment.at("task7"), "host3");
  EXPECT_NE(finalAssignment.at("task4"), "host3");
  EXPECT_NE(finalAssignment.at("task9"), "host3");
}

TEST_P(LargeShardSpecTest, BasicGoalWithNonAcceptingSpec) {
  setUpBasicProblem();

  addLargeShardGoal();

  addToFreeConstraint({"host0"});
  addCapacityConstraint("host", "memory", scopeItemToMemory_);
  addAvoidMovingConstraint({"task0", "task1", "task3"});
  addNonAcceptingConstraint({"host3"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined in setUpBasicProblem(), task7 is the
  // candidateLargeShard. host1, host2, and host3 are the potential
  // candidateContainers to drain. Of these, only host1 can be the
  // candidateContainer since host3 is part of nonAcceptingSpec and task3 in
  // host2 cannot move.

  // We expect the initial objective value at tuple index 1 to be
  // (largeShardSize - currAvailableCapacity in host1) = (10 - (12 - 5)) = 3
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 3.0, 1e-8);

  // Expect final value for goal at tuple index 1 to be zero since we expect
  // the largeShard to move into host1
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to move into host1, and task6 to have moved out
  auto finalAssignment = *solution.assignment();
  EXPECT_EQ(finalAssignment.at("task7"), "host1");
  EXPECT_NE(finalAssignment.at("task6"), "host1");
}

TEST_P(LargeShardSpecTest, BasicGoalWithNonContainerScope) {
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task8"}},
      {"host1", {"task2", "task4"}},
      {"host2", {"task3", "task9"}},
      {"host3", {"task7"}},
      {"host4", {"task1", "task0"}},
      {"host5", {"task6"}},
      {"host6", {"task5"}},
  };
  setUpBasicProblem(initialAssignment);

  {
    // add a new scope called "rack" and define a dimension "bandwidth" on this
    // scope
    solver_->addScope(
        "rack",
        folly::F14FastMap<std::string, std::string>{
            {"host0", "rack0"},
            {"host1", "rack1"},
            {"host2", "rack1"},
            {"host3", "rack0"},
            {"host4", "rack2"},
            {"host5", "rack2"},
            {"host6", "rack3"},
        });

    scopeItemToBandwidth_ = {
        {"rack0", 1000},
        {"rack1", 17},
        {"rack2", 15},
        {"rack3", 13},
    };
    // task memory values are the same as task bandwidth values
    solver_->addObjectDimension("bandwidth", taskToMemory_);

    solver_->addScopeDimension("bandwidth", "rack", scopeItemToBandwidth_);
  }

  addLargeShardGoal("rack", "bandwidth", "rack0");

  addToFreeConstraint({"host0", "host3"});
  addCapacityConstraint("rack", "bandwidth", scopeItemToBandwidth_);
  addAvoidMovingConstraint({"task8"});

  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(2, finalObjectives->size());

  // In the problem defined above and in setUpBasicProblem(), task7 (in host3)
  // is the candidateLargeShard as it the largest shard in the
  // unassignedScopeItem (rack0). rack1, rack2, and rack3 are the potential
  // candidateScopeItems to drain. Of these, one can see that we expect rack1 to
  // be the candidateScopeItem since it has the smallest DrainMetric (in this
  // case the smallest 'maxSizeShardToRemoved')

  // We expect the initial objective value at tuple index 1 to be
  // (largeShardSize - currAvailableCapacity in rack1) = (10 - (17 - 8.5)) = 1.5
  auto initialObjValue = initialObjectives->at(1).value();
  EXPECT_NEAR(*initialObjValue, 1.5, 1e-8);

  // Expect final value for goal at tuple index 1 to be zero since we expect
  // the largeShard to move into a container in rack1
  auto finalObjValue = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObjValue, 0.0);

  // We expect task7 to move into a container in rack1
  auto finalAssignment = *solution.assignment();
  EXPECT_TRUE(
      finalAssignment.at("task7") == "host1" ||
      finalAssignment.at("task7") == "host2");
}
