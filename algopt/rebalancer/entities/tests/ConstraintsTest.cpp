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
#include "algopt/rebalancer/entities/Constraints.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"

#include <gtest/gtest.h>

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::entities::tests {

TEST(ConstraintsTest, Constructor) {
  const auto constraint1Id = ConstraintId(1);
  const auto constraint2Id = ConstraintId(2);

  CapacitySpec spec1;
  spec1.scope() = "host";
  ConstraintSpecs specs1;
  specs1.capacitySpec() = std::move(spec1);
  auto constraint1 = std::make_shared<Constraint>(
      std::move(specs1),
      ConstraintPolicy::DEFAULT,
      /*invalidCost=*/1.0,
      /*invalidState=*/2.0,
      /*tuplePosIfBroken=*/0);

  CapacitySpec spec2;
  spec2.scope() = "rack";
  ConstraintSpecs specs2;
  specs2.capacitySpec() = std::move(spec2);
  auto constraint2 = std::make_shared<Constraint>(
      std::move(specs2),
      ConstraintPolicy::HARD,
      /*invalidCost=*/3.0,
      /*invalidState=*/4.0,
      /*tuplePosIfBroken=*/2);

  std::map<ConstraintId, std::shared_ptr<Constraint>> constraintIdToConstraint;
  constraintIdToConstraint.emplace(constraint1Id, std::move(constraint1));
  constraintIdToConstraint.emplace(constraint2Id, std::move(constraint2));
  const auto constraints = Constraints(std::move(constraintIdToConstraint));

  EXPECT_EQ(2, constraints.getConstraintIds().size());
  const std::set<ConstraintId> expectedConstraintIds(
      {constraint1Id, constraint2Id});
  EXPECT_EQ(
      expectedConstraintIds,
      toSet<ConstraintId>(constraints.getConstraintIds()));

  const auto& c1 = constraints.getConstraint(constraint1Id);
  EXPECT_EQ("host", *c1.getSpec().capacitySpec().value().scope());
  EXPECT_EQ(ConstraintPolicy::DEFAULT, c1.getPolicy());
  EXPECT_EQ(1.0, c1.getInvalidCost());
  EXPECT_EQ(2.0, c1.getInvalidState());
  EXPECT_EQ(0, c1.getTupleIndex());

  const auto& c2 = constraints.getConstraint(constraint2Id);
  EXPECT_EQ("rack", *c2.getSpec().capacitySpec().value().scope());
  EXPECT_EQ(ConstraintPolicy::HARD, c2.getPolicy());
  EXPECT_EQ(3.0, c2.getInvalidCost());
  EXPECT_EQ(4.0, c2.getInvalidState());
  EXPECT_EQ(2, c2.getTupleIndex());
}

TEST(ConstraintsTest, ToThrift) {
  const auto constraint0Id = ConstraintId(0);
  const auto constraint1Id = ConstraintId(1);

  CapacitySpec spec0;
  spec0.scope() = "host";
  ConstraintSpecs specs0;
  specs0.capacitySpec() = spec0;
  auto constraintSpec0 = std::make_shared<Constraint>(
      std::move(specs0), ConstraintPolicy::DEFAULT, 1.0, 2.0, 0);

  CapacitySpec spec1;
  spec1.scope() = "rack";
  ConstraintSpecs specs1;
  specs1.capacitySpec() = spec1;
  auto constraintSpec1 = std::make_shared<Constraint>(
      std::move(specs1), ConstraintPolicy::HARD, 3.0, 4.0, 2);

  std::map<ConstraintId, std::shared_ptr<Constraint>> constraintIdToConstraint;
  constraintIdToConstraint.emplace(constraint0Id, std::move(constraintSpec0));
  constraintIdToConstraint.emplace(constraint1Id, std::move(constraintSpec1));

  const Constraints constraints(std::move(constraintIdToConstraint));

  auto thrift = constraints.toThrift();

  EXPECT_EQ(2, thrift.constraints()->size());

  auto& constraint0 = thrift.constraints()->at(0);
  EXPECT_EQ(ConstraintSpecs::Type::capacitySpec, constraint0.spec()->getType());
  EXPECT_EQ(
      "host", *constraint0.spec()->capacitySpec().as_const().value().scope());
  EXPECT_EQ(ConstraintPolicy::DEFAULT, *constraint0.policy());
  EXPECT_EQ(1.0, *constraint0.invalidCost());
  EXPECT_EQ(2.0, *constraint0.invalidState());
  EXPECT_EQ(0, *constraint0.tuplePosIfBroken());

  auto& constraint1 = thrift.constraints()->at(1);
  EXPECT_EQ(ConstraintSpecs::Type::capacitySpec, constraint1.spec()->getType());
  EXPECT_EQ(
      "rack", *constraint1.spec()->capacitySpec().as_const().value().scope());
  EXPECT_EQ(ConstraintPolicy::HARD, *constraint1.policy());
  EXPECT_EQ(3.0, *constraint1.invalidCost());
  EXPECT_EQ(4.0, *constraint1.invalidState());
  EXPECT_EQ(2, *constraint1.tuplePosIfBroken());
}

TEST(ConstraintsTest, FromThrift) {
  entities::thrift::Constraints constraintsThrift;

  {
    CapacitySpec spec;
    spec.scope() = "host";
    ConstraintSpecs specs;
    specs.capacitySpec() = spec;

    entities::thrift::Constraint constraintThrift;
    constraintThrift.spec() = specs;
    constraintThrift.policy() = ConstraintPolicy::DEFAULT;
    constraintThrift.invalidCost() = 1.0;
    constraintThrift.invalidState() = 2.0;
    constraintThrift.tuplePosIfBroken() = 0;

    constraintsThrift.constraints()->emplace(0, constraintThrift);
  }

  {
    CapacitySpec spec;
    spec.scope() = "rack";
    ConstraintSpecs specs;
    specs.capacitySpec() = spec;

    entities::thrift::Constraint constraintThrift;
    constraintThrift.spec() = specs;
    constraintThrift.policy() = ConstraintPolicy::HARD;
    constraintThrift.invalidCost() = 3.0;
    constraintThrift.invalidState() = 4.0;
    constraintThrift.tuplePosIfBroken() = 2;

    constraintsThrift.constraints()->emplace(1, constraintThrift);
  }

  const Constraints constraints(constraintsThrift);

  const std::set<ConstraintId> expectedConstraintIds(
      {ConstraintId(0), ConstraintId(1)});
  EXPECT_EQ(
      expectedConstraintIds,
      toSet<ConstraintId>(constraints.getConstraintIds()));

  auto& constraint0 = constraints.getConstraint(ConstraintId(0));
  EXPECT_EQ("host", *constraint0.getSpec().capacitySpec().value().scope());
  EXPECT_EQ(ConstraintPolicy::DEFAULT, constraint0.getPolicy());
  EXPECT_EQ(1.0, constraint0.getInvalidCost());
  EXPECT_EQ(2.0, constraint0.getInvalidState());
  EXPECT_EQ(0, constraint0.getTupleIndex());

  auto& constraint1 = constraints.getConstraint(ConstraintId(1));
  EXPECT_EQ("rack", *constraint1.getSpec().capacitySpec().value().scope());
  EXPECT_EQ(ConstraintPolicy::HARD, constraint1.getPolicy());
  EXPECT_EQ(3.0, constraint1.getInvalidCost());
  EXPECT_EQ(4.0, constraint1.getInvalidState());
  EXPECT_EQ(2, constraint1.getTupleIndex());
}
} // namespace facebook::rebalancer::entities::tests
