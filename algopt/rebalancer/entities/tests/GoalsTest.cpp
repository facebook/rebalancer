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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/entities/Goal.h"
#include "algopt/rebalancer/entities/Goals.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"

#include <gtest/gtest.h>

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::entities::tests {

TEST(GoalsTest, Constructor) {
  const auto goal1Id = GoalId(1);
  const auto goal2Id = GoalId(2);

  GoalSpecs spec1, spec2;
  auto goal1 = std::make_shared<Goal>(
      std::move(spec1), /*weight=*/1.0, /*tupleIndex=*/0);
  auto goal2 = std::make_shared<Goal>(
      std::move(spec2), /*weight=*/10.0, /*tupleIndex=*/5);

  std::map<GoalId, std::shared_ptr<Goal>> goalIdToGoal;
  goalIdToGoal.emplace(goal1Id, std::move(goal1));
  goalIdToGoal.emplace(goal2Id, std::move(goal2));

  const auto goals = Goals(std::move(goalIdToGoal));

  EXPECT_EQ(2, goals.getGoalIds().size());
  EXPECT_EQ(6, goals.getTupleSize());
  EXPECT_EQ(1.0, goals.getGoal(goal1Id).getWeight());
  EXPECT_EQ(0, goals.getGoal(goal1Id).getTupleIndex());
  EXPECT_EQ(10.0, goals.getGoal(goal2Id).getWeight());
  EXPECT_EQ(5, goals.getGoal(goal2Id).getTupleIndex());

  // Verify indexedGoalIds is correctly populated
  EXPECT_EQ(std::vector<GoalId>({goal1Id}), goals.getGoalIds(0));
  EXPECT_EQ(std::vector<GoalId>({goal2Id}), goals.getGoalIds(5));
  // Empty for intermediate indices
  EXPECT_EQ(std::vector<GoalId>(), goals.getGoalIds(1));
  EXPECT_EQ(std::vector<GoalId>(), goals.getGoalIds(2));
}

TEST(GoalsTest, ToThrift) {
  const auto goal0Id = GoalId(0);
  const auto goal1Id = GoalId(1);

  CapacitySpec spec0;
  spec0.scope() = "host";
  GoalSpecs specs0;
  specs0.capacitySpec() = spec0;
  auto goalSpec0 = std::make_shared<Goal>(std::move(specs0), 4.0, 0);

  CapacitySpec spec1;
  spec1.scope() = "rack";
  GoalSpecs specs1;
  specs1.capacitySpec() = spec1;
  auto goalSpec1 = std::make_shared<Goal>(std::move(specs1), 3.0, 2);

  std::map<GoalId, std::shared_ptr<Goal>> goalIdToGoal;
  goalIdToGoal.emplace(goal0Id, std::move(goalSpec0));
  goalIdToGoal.emplace(goal1Id, std::move(goalSpec1));

  const Goals goals(std::move(goalIdToGoal));

  auto thrift = goals.toThrift();

  EXPECT_EQ(2, thrift.goals()->size());

  auto& goal0 = thrift.goals()->at(0);
  EXPECT_EQ(GoalSpecs::Type::capacitySpec, goal0.spec()->getType());
  EXPECT_EQ("host", *goal0.spec()->capacitySpec().as_const().value().scope());
  EXPECT_EQ(4.0, *goal0.weight());
  EXPECT_EQ(0, *goal0.tupleIndex());

  auto& goal1 = thrift.goals()->at(1);
  EXPECT_EQ(GoalSpecs::Type::capacitySpec, goal1.spec()->getType());
  EXPECT_EQ("rack", *goal1.spec()->capacitySpec().as_const().value().scope());
  EXPECT_EQ(3.0, *goal1.weight());
  EXPECT_EQ(2, *goal1.tupleIndex());
}

TEST(GoalsTest, FromThrift) {
  entities::thrift::Goals goalsThrift;

  {
    CapacitySpec spec;
    spec.scope() = "host";
    GoalSpecs specs;
    specs.capacitySpec() = spec;

    entities::thrift::Goal goalThrift;
    goalThrift.spec() = specs;
    goalThrift.weight() = 4.0;
    goalThrift.tupleIndex() = 0;

    goalsThrift.goals()->emplace(0, goalThrift);
  }

  {
    CapacitySpec spec;
    spec.scope() = "rack";
    GoalSpecs specs;
    specs.capacitySpec() = spec;

    entities::thrift::Goal goalThrift;
    goalThrift.spec() = specs;
    goalThrift.weight() = 3.0;
    goalThrift.tupleIndex() = 2;

    goalsThrift.goals()->emplace(1, goalThrift);
  }

  const Goals goals(goalsThrift);

  const std::set<GoalId> expectedGoalIds({GoalId(0), GoalId(1)});
  EXPECT_EQ(expectedGoalIds, toSet<GoalId>(goals.getGoalIds()));
  EXPECT_EQ(3, goals.getTupleSize());
  EXPECT_EQ(std::vector<GoalId>({GoalId(0)}), goals.getGoalIds(0));
  EXPECT_EQ(std::vector<GoalId>(), goals.getGoalIds(1));
  EXPECT_EQ(std::vector<GoalId>({GoalId(1)}), goals.getGoalIds(2));

  auto& goal0 = goals.getGoal(GoalId(0));
  EXPECT_EQ("host", *goal0.getSpec().capacitySpec().value().scope());
  EXPECT_EQ(4.0, goal0.getWeight());
  EXPECT_EQ(0, goal0.getTupleIndex());

  auto& goal1 = goals.getGoal(GoalId(1));
  EXPECT_EQ("rack", *goal1.getSpec().capacitySpec().value().scope());
  EXPECT_EQ(3.0, goal1.getWeight());
  EXPECT_EQ(2, goal1.getTupleIndex());
}
} // namespace facebook::rebalancer::entities::tests
