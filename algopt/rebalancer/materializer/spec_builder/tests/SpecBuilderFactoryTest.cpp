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

#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilderFactory.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"

#include <gtest/gtest.h>

#include <memory>

namespace facebook::rebalancer::materializer::tests {
using facebook::rebalancer::LogCollector;

class SpecBuilderFactoryTest : public SpecBuilderTestBase<> {
 protected:
  void SetUp() override {
    setUpUniverse(
        {{"host0", {"task0", "task1"}},
         {"host1", {"task2"}},
         {"host2", {}},
         {"host3", {"task3", "task4", "task5"}}});
  }
};

TEST_F(SpecBuilderFactoryTest, CapacitySpecGoal) {
  const SpecBuilderFactory factory(
      buildUniverse(), false, std::make_shared<LogCollector>());

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  interface::ConstraintSpecs constraint;
  constraint.capacitySpec() = spec;

  auto builder = factory.getBuilder(constraint);

  EXPECT_EQ("after(task_count) <= 1 for scope host", builder->description());
}

TEST_F(SpecBuilderFactoryTest, CapacitySpecConstraint) {
  const SpecBuilderFactory factory(
      buildUniverse(), false, std::make_shared<LogCollector>());

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  interface::GoalSpecs goal;
  goal.capacitySpec() = spec;

  auto builder = factory.getBuilder(goal);

  EXPECT_EQ("after(task_count) <= 1 for scope host", builder->description());
}

} // namespace facebook::rebalancer::materializer::tests
