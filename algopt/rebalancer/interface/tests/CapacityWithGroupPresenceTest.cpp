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
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <map>
#include <string>

using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;

namespace facebook::rebalancer::interface::tests {

class CapacityWithGroupPresenceTest
    : public ::testing::TestWithParam<
          std::tuple<int, SolverAlgoType, OptimalSolverPackage>> {
 protected:
  // Scale factors for the default NORMALIZED_CONTINUOUS_UTILIZATION penalty
  // (= penaltyBound / (numObjects * maxDimValue)). Fixture has 10 objects
  // (tenant1=7, tenant2=3), maxDimValue = 1.88, roundUp=true so
  // penaltyBound = 0.5.
  static constexpr double kMaxDimValue = 1.88;
  static constexpr double kNormPerScopeItem = 0.5 / (10 * kMaxDimValue);
  static constexpr double kNormGroupFilteredTenant1 = 0.5 / (7 * kMaxDimValue);
  static constexpr double kNormTenant2 = 0.5 / (3 * kMaxDimValue);

  static int getThreadCount() {
    const auto [threadCount, algoType, solver] = GetParam();
    return threadCount;
  }
  static SolverAlgoType getSolverAlgoType() {
    const auto [threadCount, algoType, solver] = GetParam();
    return algoType;
  }
  static OptimalSolverPackage getSolverPackage() {
    const auto [threadCount, algoType, solver] = GetParam();
    return solver;
  }
  void SetUp() override {
    if (getSolverAlgoType() == OPTIMAL) {
      const auto solverPackage = getSolverPackage();
      if (isSolverUnavailable(solverPackage)) {
        GTEST_SKIP() << solverName(solverPackage) << " solver not available";
      }
    }
  }
  void setUpProblem() {
    solver =
        initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
    solver->setObjectName("trafficObject");
    solver->setContainerName("host");

    solver->setAssignment(
        std::vector<std::pair<std::string, std::vector<std::string>>>{
            {"host1", {"trafficObject8"}},
            {"host2", {"trafficObject1", "trafficObject5", "trafficObject9"}},
            {"host3", {"trafficObject2", "trafficObject6"}},
            {"host4", {"trafficObject3"}},
            {"host5", {"trafficObject4", "trafficObject7"}},
            {"host6", {"trafficObject10"}},
        });

    // add a region scope; host6 is not part of any region
    solver->addScope(
        "region",
        std::vector<std::pair<std::string, std::vector<std::string>>>{
            {"region1", {"host1", "host2"}},
            {"region2", {"host3", "host4", "host5"}}});

    // set dynamicObjectDimension
    const folly::F14FastMap<std::string, double> baseObjectToValue = {
        {"trafficObject1", 0.85},
        {"trafficObject2", 0.4},
        {"trafficObject3", 0.6},
        {"trafficObject4", 0.5},
        {"trafficObject5", 0.13},
        {"trafficObject6", 0.115},
        {"trafficObject7", 0.88},
        {"trafficObject8", 1},
        {"trafficObject9", 1.2},
        {"trafficObject10", 0.3},
    };
    std::map<std::string, folly::F14FastMap<std::string, double>>
        scopeItemToObjectToDimensionValue;
    scopeItemToObjectToDimensionValue["region1"] = baseObjectToValue;
    // only difference between object dimension values for region1 and region2
    // is the value of trafficObject7
    scopeItemToObjectToDimensionValue["region2"] = baseObjectToValue;
    scopeItemToObjectToDimensionValue["region2"]["trafficObject7"] = 1.88;

    solver->addDynamicObjectDimension(
        "replicaCount", "region", scopeItemToObjectToDimensionValue, 1.0);

    // add partition
    tenantToTrafficObjects["tenant1-trafficObjects"] = {
        "trafficObject1",
        "trafficObject2",
        "trafficObject3",
        "trafficObject4",
        "trafficObject5",
        "trafficObject9",
        "trafficObject10"};
    tenantToTrafficObjects["tenant2-trafficObjects"] = {
        "trafficObject6", "trafficObject7", "trafficObject8"};
    solver->addPartition("tenantTrafficObjects", tenantToTrafficObjects);

    // add tenantGroups partition which is a partition with three groups all
    // objects, tenant1-only, tenant2-only
    std::map<std::string, std::vector<std::string>>
        tenantGroupsToTrafficObjects;
    const auto& tenant1Objects =
        tenantToTrafficObjects.at("tenant1-trafficObjects");
    const auto& tenant2Objects =
        tenantToTrafficObjects.at("tenant2-trafficObjects");

    auto& allTenantsObjects = tenantGroupsToTrafficObjects["allTenants"];
    allTenantsObjects = tenant1Objects;
    allTenantsObjects.insert(
        allTenantsObjects.end(), tenant2Objects.begin(), tenant2Objects.end());

    tenantGroupsToTrafficObjects["tenant1Only"] =
        tenantToTrafficObjects["tenant1-trafficObjects"];

    tenantGroupsToTrafficObjects["tenant2Only"] =
        tenantToTrafficObjects["tenant2-trafficObjects"];

    solver->addPartition("tenantGroups", tenantGroupsToTrafficObjects);

    switch (getSolverAlgoType()) {
      case OPTIMAL: {
        auto optimalSpec = OptimalSolverSpec{};
        optimalSpec.solverPackage() = getSolverPackage();
        solver->addSolver(optimalSpec);
        break;
      }
      case LOCALSEARCH:
        auto spec = LocalSearchSolverSpec();
        spec.moveTypeList() = {
            ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{})};
        solver->addSolver(spec);
        break;
    }
  }

  struct SpecParams {
    bool isConstraint;
    CapacityWithGroupPresenceBound bound = CapacityWithGroupPresenceBound::MAX;
    std::optional<Limit> groupToPresenceWeight = std::nullopt;
    std::vector<interface::Limit> multipliers = {};
    std::vector<interface::GroupUtilMultiplier> groupUtilMultipliers = {};
    std::optional<Limit> groupToExtraAdditivePenalty = std::nullopt;
    std::optional<interface::Filter> groupFilter = std::nullopt;
    interface::CapacityWithGroupPresenceUsageIntent intent =
        interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM;
    std::optional<Limit> capacityLimits = std::nullopt;
    std::string partition = "tenantTrafficObjects";
    std::optional<std::string> aggregationPartition = std::nullopt;
  };
  void addCapacityWithGroupPresenceSpec(const SpecParams& specParams) {
    CapacityWithGroupPresenceSpec spec;
    spec.dimension() = "replicaCount";
    spec.partition() = specParams.partition;
    spec.scope() = "region";
    spec.roundUpGroupUtilOnScopeItem() = true;
    spec.bound() = specParams.bound;
    spec.multiplierList() = specParams.multipliers;
    spec.groupUtilMultipliers() = specParams.groupUtilMultipliers;
    spec.intent() = specParams.intent;
    spec.aggregationPartition().from_optional(specParams.aggregationPartition);

    if (specParams.capacityLimits) {
      spec.scopeItemToLimit() = *specParams.capacityLimits;
    } else {
      auto& capacityLimits = *spec.scopeItemToLimit();
      capacityLimits.type() = interface::LimitType::ABSOLUTE;
      capacityLimits.globalLimit() = 5;
      capacityLimits.scopeItemLimits() = {{"region1", 2}};
    }

    if (specParams.groupToPresenceWeight) {
      spec.groupToPresenceWeight() = *specParams.groupToPresenceWeight;
    } else {
      auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
      groupToPresenceWeight.globalLimit() = 2;
      groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};
    }

    if (specParams.groupToExtraAdditivePenalty) {
      spec.groupToExtraAdditivePenalty() =
          *specParams.groupToExtraAdditivePenalty;
    }

    if (specParams.groupFilter) {
      spec.groupFilter() = *specParams.groupFilter;
    }

    if (specParams.isConstraint) {
      solver->addConstraint(spec);
    } else {
      solver->addGoal(spec, 1.0);
    }
  }

  std::unique_ptr<ProblemSolver> solver;
  std::map<std::string, std::vector<std::string>> tenantToTrafficObjects;
};

INSTANTIATE_TEST_CASE_P(
    LocalSearch,
    CapacityWithGroupPresenceTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(LOCALSEARCH),
        ::testing::Values(
            OptimalSolverPackage::GUROBI))); // ignored but required

INSTANTIATE_TEST_CASE_P(
    Optimal,
    CapacityWithGroupPresenceTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(OPTIMAL),
        testSolverPackages()));

TEST_P(CapacityWithGroupPresenceTest, MaxConstraint) {
  setUpProblem();

  addCapacityWithGroupPresenceSpec(SpecParams{.isConstraint = true});

  const auto solution = solver->solve();
  const auto initialObjectiveValue =
      *solution.initialGlobalObjective()->goals()->at(0).value();
  const auto finalObjectiveValue =
      *solution.finalGlobalObjective()->goals()->at(0).value();

  switch (getSolverAlgoType()) {
    case LOCALSEARCH: {
      // ---------------------
      // region1:
      //---------------------
      // tenant1-trafficObjects' ceiled contribution to region1 util =
      // ceil(0.85+0.13+1.2) = ceil(2.18) = 3
      // ---with minPresenceWeight of 3 = max(3, 3) = 3
      // tenant2-trafficObjects' ceiled contribution to region1 util = ceil(1.0)
      // = 1.0
      // ---with minPresenceWeight of 2 = max(1, 2) = 2
      // region1 constraintExpr = (3+2) - 2 = 3
      // penaltyExpr = 2.18 + 1.0 = 3.18
      // ---------------------
      // region2:
      // ---------------------
      // tenant1-trafficObjects' ceiled contribution to region2 util =
      // ceil(0.4+0.6+0.5) = ceil(1.5) = 2
      // ---with minPresenceWeight of 3 = max(3, 2) = 3
      // tenant2-trafficObjects' ceiled contribution to region2 util =
      // ceil(0.115
      // + 1.88) = ceil(1.995) = 2
      // ---with minPresenceWeight of 2 = max(2, 2) = 2
      // region2 constraintExpr = (3 + 2) - 5 = 0
      // penaltyExpr = 0 (no penalty because constraint is not broken)
      //-----
      // goal value without invalidState and invalidCost
      // = 3 (constraint) + 3.18 (raw penalty) * kNormPerScopeItem
      // with default invalidState and cost
      // = 10000 + 100 * (3 + 3.18 * kNormPerScopeItem)
      EXPECT_NEAR(
          10000 + 100 * (3 + 3.18 * kNormPerScopeItem),
          initialObjectiveValue,
          1e-8);

      // it is possible to fix this constraint by moving objects to host6 which
      // is outside of region scope
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
    case OPTIMAL: {
      // same as above but without penalty values
      EXPECT_NEAR(10300.0, initialObjectiveValue, 1e-8);
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
  }
}

TEST_P(CapacityWithGroupPresenceTest, MaxConstraintLocalSearchWithMultipliers) {
  setUpProblem();

  std::vector<interface::Limit> multipliers(2, interface::Limit());
  multipliers[0].type() = interface::LimitType::ABSOLUTE;
  multipliers[0].scopeItemLimits() = {{"region1", 1.2}};
  multipliers[1].type() = interface::LimitType::ABSOLUTE;
  multipliers[1].globalLimit() = 4;
  multipliers[1].groupLimits() = {{"tenant2-trafficObjects", 8}};

  addCapacityWithGroupPresenceSpec(
      SpecParams{
          .isConstraint = true,
          .bound = CapacityWithGroupPresenceBound::MAX,
          .multipliers = std::move(multipliers)});

  const auto solution = solver->solve();
  const auto initialObjectiveValue =
      *solution.initialGlobalObjective()->goals()->at(0).value();
  const auto finalObjectiveValue =
      *solution.finalGlobalObjective()->goals()->at(0).value();

  switch (getSolverAlgoType()) {
    case LOCALSEARCH: {
      /*
      constraint w.r.t. both region1 and region2 are broken;
      w.r.t. region1:
        tenant1-trafficObjects' contribution to region1 without multpliers
          = max(3, 0.85 + 0.13 + 1.2) = ceil(max(3, 2.18)) = 3
        with multipliers = ceil( ceil(3 * 1.2) * 4.0) = 16.0

        tenant2-trafficObjects' contribution to region1 without multpliers
        = max(2, 1.0) = ceil(max(2, 1)) = 2
        with multipliers = ceil( ceil(2 * 1.2) * 8.0) = 24

       w.r.t. region2:
        tenant1-trafficObjects' contribution to region2 without multpliers
          = max(3, 0.4+0.6+0.5) = ceil(max(3, 1.5)) = 3
        with multipliers = ceil(ceil(3 * 1.0) * 4.0) = 12.0

        tenant2-trafficObjects' contribution to region2 without multpliers
        = max(2, 0.115+1.88) = ceil(max(2, 1.995)) = 2
        with multipliers = ceil( ceil(2 * 1.0) * 8.0) = 16.0

       goal value without invalidState and invalidCost
       = (constraint sum 61) + (raw penalty sum 42.024).
       Raw penalty is scaled by kNormPerScopeItem under NORMALIZED.
      with default invalidState and cost = 10000 * 2 (two broken constraints) +
      100 * (61 + 42.024 * kNormPerScopeItem)
      */
      EXPECT_NEAR(
          20000 + 100 * (61 + 42.024 * kNormPerScopeItem),
          initialObjectiveValue,
          1e-8);

      // it is possible to fix this constraint by moving objects to host6 which
      // is outside of region scope
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
    case OPTIMAL: {
      // same as above but without penalty terms
      // goal value without invalidState and invalidCost =  ((16.0 + 24.0 - 2.0)
      // + (12.0 + 16.0 - 5.0) =  38+23 = 61
      // with default invalidState and cost = 10000 * 2 (two broken constraints)
      // + 100*(61) = 30302.4
      EXPECT_NEAR(26100, initialObjectiveValue, 1e-8);
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
  }
}

TEST_P(
    CapacityWithGroupPresenceTest,
    MaxConstraintLocalSearchWithExtraPenalty) {
  /*
  The goal of this test is to show the usefulness of the extra penalty term. In
  this example, we have two tenants (tenant1-trafficObjects and
  tenant2-trafficObjects) and two regions (region1 and region2). One of the
  objects in tenant1-trafficObjects, trafficObject5, which is initially in host2
  (region1), is immovable, and tenant1-trafficObjects has a minPresenceWeight
  of 10. Note that since trafficObject5 is immovable, the constraint w.r.t.
  region1 is always going to be broken. However, when using local search, we
  have an extra penalty term (=sum of group utilizations) whenever the
  constraint is broken, which will in turn try to force objects in region1 to
  move out. But again note that moving any of the objects of
  tenant1-trafficObjects is not useful because the constraint can never be fixed
  (because trafficObject5 is immovable and minPresenceWeight = 10), and hence
  might be undesirable because we have another goal to minimize movements. (This
  only a problem in local search because of way penalties are imposed; optimal
  solver won't have such an issue)
  */
  setUpProblem();

  // make trafficObject5 immovable
  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.name() = "do not move trafficObject5";
  avoidMovingSpec.objects() = {"trafficObject5"};
  solver->addConstraint(avoidMovingSpec);

  // tenant1-trafficObjects have a minPresenceWeight of 10 in region1, and 3
  // in region2
  Limit groupToPresenceWeight;
  groupToPresenceWeight.type() = interface::LimitType::ABSOLUTE;
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 10}};
  groupToPresenceWeight.scopeItemToGroupLimits() = {
      {
          "region2",
          {
              {"tenant1-trafficObjects", 3},
          },
      },
  };
  Limit groupToExtraAdditivePenalty;
  groupToExtraAdditivePenalty.type() = interface::LimitType::ABSOLUTE;
  groupToExtraAdditivePenalty.globalLimit() = 0.0;
  groupToExtraAdditivePenalty.scopeItemToGroupLimits() = {
      {
          "region1",
          {
              {"tenant1-trafficObjects", -10},
          },
      },
      {
          "region2",
          {
              {"tenant1-trafficObjects", -3},
          },
      },
  };
  addCapacityWithGroupPresenceSpec(
      SpecParams{
          .isConstraint = true,
          .groupToPresenceWeight = std::move(groupToPresenceWeight),
          .groupToExtraAdditivePenalty = std::move(groupToExtraAdditivePenalty),
      });

  // add a low priority goal to minimize movement
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "trafficObject_count";
  capacitySpec.definition() = CapacitySpecDefinition::NEW;
  capacitySpec.limit()->globalLimit() = 0.0;
  solver->addGoal(capacitySpec, /*weight=*/1, /*tuplePos=*/3);

  const auto solution = solver->solve();
  const auto initialObjectiveValue =
      *solution.initialGlobalObjective()->goals()->at(0).value();
  const auto finalObjectiveValue =
      *solution.finalGlobalObjective()->goals()->at(0).value();

  switch (getSolverAlgoType()) {
    case LOCALSEARCH: {
      // constraint w.r.t. both region1 and region2 are broken;
      // region1:
      //--------
      // tenant1-trafficObjects' ceiled contribution to region1 util =
      // ceil(0.85+0.13+1.2) = ceil(2.18) = 3
      // ---with minPresenceWeight of 10 = max(3, 10) = 10
      // tenant2-trafficObjects' ceiled contribution to region1 util = ceil(1.0)
      // = 1.0
      // ---with minPresenceWeight of 2 = max(1, 2) = 2
      // region1 constraintExpr = (10+2) - 2 = 10
      // penaltyExpr = (max(2.18 - 10, 0))  + 1.0 = 1.0  // not how
      // additivePenalty is included
      // ---------------------
      // region2:
      //--------
      // tenant1-trafficObjects' ceiled contribution to region2 util =
      // ceil(0.4+0.6+0.5) = ceil(1.5) = 2
      // ---with minPresenceWeight of 3 = max(3, 2) = 3
      // tenant2-trafficObjects' ceiled contribution to region2 util =
      // ceil(0.115
      // + 1.88) = ceil(1.995) = 2
      // ---with minPresenceWeight of 2 = max(2, 2) = 2
      // region2 constraintExpr = (3 + 2) - 5 = 0
      // penaltyExpr = 0 (no penalty because constraint is not broken)
      //-----
      // tuple0 goal value without invalidState and invalidCost
      // = 10 (constraint) + 1.0 (raw penalty) * kNormPerScopeItem
      // with default invalidState and cost
      // = 10000 + 100 * (10 + 1.0 * kNormPerScopeItem)
      EXPECT_NEAR(
          10000 + 100 * (10 + 1.0 * kNormPerScopeItem),
          initialObjectiveValue,
          1e-8);

      // we expect exactly one move where trafficObject8 moves from host1 to
      // host6; as a result, constraintExpr w.r.t. region1 becomes (10 + 0 - 2)
      // = 8; penaty expression will now be 0 because tenant1-trafficObject have
      // an additive penalty term of -10; tuple0 goal value without invalidState
      // and invalidCost = (8 + 0) + (0 + 0) = 8 with default invalidState and
      // cost = 10000 +  100*(8) = 10800
      EXPECT_NEAR(10800.0, finalObjectiveValue, 1e-8);
      break;
    }
    case OPTIMAL: {
      // same as above but without penalty values
      EXPECT_NEAR(11000.0, initialObjectiveValue, 1e-8);
      EXPECT_NEAR(10800.0, finalObjectiveValue, 1e-8);
      break;
    }
  }

  // we do no expect any objects of tenant1-trafficObjects
  const auto& initialAssignment = *solution.initialAssignment();
  const auto& finalAssignment = *solution.assignment();
  for (const auto& object : tenantToTrafficObjects["tenant1-trafficObjects"]) {
    const auto& initialHost = initialAssignment.at(object);
    const auto& finalHost = finalAssignment.at(object);
    EXPECT_EQ(finalHost, initialHost);
  }

  // we expect trafficObject8 to have moved out of host1 to host6 because
  // that improves the brokeness of the constraint
  EXPECT_EQ("host6", finalAssignment.at("trafficObject8"));
}

TEST_P(CapacityWithGroupPresenceTest, MaxConstraintWithGroupFilter) {
  // This test is the same MaxConstraint test but where we use a group filter
  setUpProblem();

  // Create a group filter that only includes "tenant1-trafficObjects"
  interface::Filter groupFilter;
  groupFilter.type() = interface::FilterType::GROUP;
  groupFilter.itemsWhitelist() = {"tenant1-trafficObjects"};

  addCapacityWithGroupPresenceSpec(
      SpecParams{.isConstraint = true, .groupFilter = groupFilter});

  const auto solution = solver->solve();
  const auto initialObjectiveValue =
      *solution.initialGlobalObjective()->goals()->at(0).value();
  const auto finalObjectiveValue =
      *solution.finalGlobalObjective()->goals()->at(0).value();

  switch (getSolverAlgoType()) {
    case LOCALSEARCH: {
      // Since only tenant1-trafficObjects is part of whitelist, only that
      // tenant contributes to the utilization:
      // ---------------------
      // region1:
      //---------------------
      // tenant1-trafficObjects' ceiled contribution to region1 util =
      // ceil(0.85+0.13+1.2) = ceil(2.18) = 3
      // ---with minPresenceWeight of 3 = max(3, 3) = 3
      // tenant2-trafficObjects is filtered out, so no contribution
      // region1 constraintExpr = 3 - 2 = 1
      // penaltyExpr = 2.18 (only tenant1 contribution)
      // ---------------------
      // region2:
      // ---------------------
      // tenant1-trafficObjects' ceiled contribution to region2 util =
      // ceil(0.4+0.6+0.5) = ceil(1.5) = 2
      // ---with minPresenceWeight of 3 = max(3, 2) = 3
      // tenant2-trafficObjects is filtered out, so no contribution
      // region2 constraintExpr = 3 - 5 = -2 (constraint satisfied)
      // penaltyExpr = 0 (no penalty because constraint is not broken)
      //-----
      // goal value without invalidState and invalidCost
      // = 1 (constraint) + 2.18 (raw penalty) * kNormGroupFilteredTenant1
      // (groupFilter restricts the penalty scale to tenant1's 7 objects).
      // with default invalidState and cost
      // = 10000 + 100 * (1 + 2.18 * kNormGroupFilteredTenant1)
      EXPECT_NEAR(
          10000 + 100 * (1 + 2.18 * kNormGroupFilteredTenant1),
          initialObjectiveValue,
          1e-8);

      // it is possible to fix this constraint by moving objects to host6 which
      // is outside of region scope
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
    case OPTIMAL: {
      // same as above but without penalty values
      // goal value = 1 + 0 = 1
      // with default invalidState and cost = 10000 + 100*(1) = 10100
      EXPECT_NEAR(10100.0, initialObjectiveValue, 1e-8);
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
  }
}

TEST_P(
    CapacityWithGroupPresenceTest,
    MaxConstraintWithMultipliersAndPerGroupAndScopeItem) {
  setUpProblem();

  std::vector<interface::Limit> multipliers(2, interface::Limit());
  multipliers[0].type() = interface::LimitType::ABSOLUTE;
  multipliers[0].scopeItemLimits() = {{"region1", 1.2}};
  multipliers[1].type() = interface::LimitType::ABSOLUTE;
  multipliers[1].globalLimit() = 4;
  multipliers[1].groupLimits() = {{"tenant2-trafficObjects", 8}};

  auto capacityLimits = interface::Limit();
  capacityLimits.type() = interface::LimitType::ABSOLUTE;
  capacityLimits.globalLimit() = 13;
  capacityLimits.scopeItemLimits() = {{"region1", 16}};
  capacityLimits.scopeItemToGroupLimits() = {
      {"region2", {{"tenant2-trafficObjects", 10}}},
  };

  addCapacityWithGroupPresenceSpec(
      SpecParams{
          .isConstraint = true,
          .bound = CapacityWithGroupPresenceBound::MAX,
          .multipliers = std::move(multipliers),
          .intent =
              CapacityWithGroupPresenceUsageIntent::PER_GROUP_AND_SCOPE_ITEM,
          .capacityLimits = std::move(capacityLimits),
      });

  const auto solution = solver->solve();
  const auto initialObjectiveValue =
      *solution.initialGlobalObjective()->goals()->at(0).value();
  const auto finalObjectiveValue =
      *solution.finalGlobalObjective()->goals()->at(0).value();

  switch (getSolverAlgoType()) {
    case LOCALSEARCH: {
      /*
      tenant1, region1:
        tenant1-trafficObjects' contribution to region1 without multpliers
          = max(3, 0.85 + 0.13 + 1.2) = ceil(max(3, 2.18)) = 3
        with multipliers = ceil( ceil(3 * 1.2) * 4.0) = 16.0

      tenant2, region1:
        tenant2-trafficObjects' contribution to region1 without multpliers
        = max(2, 1.0) = ceil(max(2, 1)) = 2
        with multipliers = ceil( ceil(2 * 1.2) * 8.0) = 24

      tenant1, region2:
        tenant1-trafficObjects' contribution to region2 without multpliers
          = max(3, 0.4+0.6+0.5) = ceil(max(3, 1.5)) = 3
        with multipliers = ceil(ceil(3 * 1.0) * 4.0) = 12.0

      tenant2, region2:
        tenant2-trafficObjects' contribution to region2 without multpliers
        = max(2, 0.115+1.88) = ceil(max(2, 1.995)) = 2
        with multipliers = ceil( ceil(2 * 1.0) * 8.0) = 16.0

      goal value without invalidState and invalidCost =  ((16.0 - 16.0) + 0)
      ((24.0 - 16.0) + 1.0*1.2*8) +
      ((12.0 - 13) + 0) +
      ((16.0 - 10) + 1.995*1*8)
      The two broken constraints (tenant2,region1) and (tenant2,region2)
      contribute constraint sum = 14 and raw penalty sum = 25.56, both on
      tenant2 (3 objects). Raw penalty is scaled by kNormTenant2. with default
      invalidState and cost = 10000 * 2 (two broken constraints) + 100 * (14
      + 25.56 * kNormTenant2)
      */
      EXPECT_NEAR(
          20000 + 100 * (14 + 25.56 * kNormTenant2),
          initialObjectiveValue,
          1e-8);

      // it is possible to fix this constraint by moving objects to host6 which
      // is outside of region scope
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
    case OPTIMAL: {
      // same as above but without penalty terms
      // goal value without invalidState and invalidCost =  (16.0 - 16.0) +
      // (24.0 - 16.0) + (12.0 - 13) + (16.0 - 10) =  0 + 8 + 0 + 6 = 14
      // with default invalidState and cost = 10000 * 2 (two broken
      //  constraints) + 100*(14) = 21400
      EXPECT_NEAR(21400, initialObjectiveValue, 1e-8);
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
  }
}

TEST_P(
    CapacityWithGroupPresenceTest,
    ErroWhenUsingGroupLimitsAndPerScopeItemIntent) {
  setUpProblem();

  auto capacityLimits = interface::Limit();
  capacityLimits.type() = interface::LimitType::ABSOLUTE;
  capacityLimits.globalLimit() = 13;
  capacityLimits.groupLimits() = {{"tenant1-trafficObjects", 16}};
  capacityLimits.scopeItemToGroupLimits() = {
      {"region2", {{"tenant2-trafficObjects", 10}}},
  };

  REBALANCER_EXPECT_RUNTIME_ERROR(
      addCapacityWithGroupPresenceSpec(
          SpecParams{
              .isConstraint = true,
              .intent = CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM,
              .capacityLimits = std::move(capacityLimits),
          }),
      "unexpected group limits");
}

TEST_P(
    CapacityWithGroupPresenceTest,
    MaxConstraintWithMultipliersPerGroupAndScopeItemAggregationPartition) {
  REBALANCER_SKIP_IF_NO_MANIFOLD();
  setUpProblem();

  std::vector<interface::Limit> multipliers(2, interface::Limit());
  multipliers[0].type() = interface::LimitType::ABSOLUTE;
  multipliers[0].scopeItemLimits() = {{"region1", 1.2}};
  multipliers[1].type() = interface::LimitType::ABSOLUTE;
  multipliers[1].globalLimit() = 4;
  multipliers[1].groupLimits() = {{"tenant2-trafficObjects", 8}};

  auto capacityLimits = interface::Limit();
  capacityLimits.type() = interface::LimitType::ABSOLUTE;
  capacityLimits.globalLimit() = 13;
  capacityLimits.scopeItemLimits() = {{"region1", 16}};
  capacityLimits.scopeItemToGroupLimits() = {
      {"region2", {{"tenant2Only", 10}}},
  };

  addCapacityWithGroupPresenceSpec(
      SpecParams{
          .isConstraint = true,
          .bound = CapacityWithGroupPresenceBound::MAX,
          .multipliers = std::move(multipliers),
          .intent =
              CapacityWithGroupPresenceUsageIntent::PER_GROUP_AND_SCOPE_ITEM,
          .capacityLimits = std::move(capacityLimits),
          .partition = "tenantGroups",
          .aggregationPartition = "tenantTrafficObjects",
      });

  const auto solution = solver->solve();
  solver->persistToManifold();
  const auto initialObjectiveValue =
      *solution.initialGlobalObjective()->goals()->at(0).value();
  const auto finalObjectiveValue =
      *solution.finalGlobalObjective()->goals()->at(0).value();

  switch (getSolverAlgoType()) {
    case LOCALSEARCH: {
      /*
        allTenants, region1:
          allTenants group in mainPartition consists of tenant1 and tenant2, so
          aggregation happens based on tenant1 and tenant2

            tenant1-trafficObjects' rounded-up contribution to region1
            without multpliers = ceil(max(3, 0.85 + 0.13 + 1.2)) = 3
            with multipliers = ceil( ceil(3 * 1.2) * 4.0) = 16.0

            tenant2-trafficObjects' rounded-up contribution to region1
            without multpliers = ceil(max(2, 1.0)) = 2
            with multipliers = ceil( ceil(2 * 1.2) * 8.0) = 24

            allTenants' contribution to region1 = 16 + 24 = 40

        tenant1-only, region1:
            tenant1-only group in mainPartition consists of tenant1, so same
        computation as above w.r.t. tenant1-trafficObjects

        tenant2-only, region1:
          tenant2-only group in mainPartition consists of tenant2, so same
          computation as above w.r.t. tenant2-trafficObjects

        allTenants, region2:
          allTenants group in mainPartition consists of tenant1 and tenant2, so
          aggregation happens based on tenant1 and tenant2

            tenant1-trafficObjects' rounded-up contribution to region2
            without multpliers = ceil(max(3, 0.4+0.6+0.5)) = 3
            with multipliers = ceil(ceil(3 * 1.0) * 4.0) = 12.0

            tenant2-trafficObjects' rounded-up contribution to region2
            without multpliers = ceil(max(2, 0.115+1.88)) = 2
            with multipliers = ceil( ceil(2 * 1.0) * 8.0) = 16.0

            allTenants' contribution to region2 = 12 + 16 = 28

        tenant1-only, region2:
            tenant1-only group in mainPartition consists of tenant1, so same
        computation as above w.r.t. tenant1-trafficObjects

        tenant2-only, region2:
          tenant2-only group in mainPartition consists of tenant2, so same
          computation as above w.r.t. tenant2-trafficObjects


        goal value without invalidState and invalidCost =
        ((40 - 16) + ((0.85+0.13+1.2)*1.2*4) + (1.0*1.2*8) +
        ((16-16) + 0) +
        ((24 - 16) + 1.0*1.2*8) +
        ((28 - 13) + ((0.4+0.6+0.5)*1*4) + ((0.115+1.88)*1*8)
        ((12-13) + 0) +
        ((16-10) + (0.115+1.88)*1*8)
        = 44.064 + 0 + 17.6 + 36.96 + 0 + 21.96
        = (constraint sum 53) + (raw penalty sum 67.584). Under NORMALIZED the
        penalty splits by main-partition group: allTenants (10 objects)
        contributes 42.024 * kNormPerScopeItem; tenant2-only (3 objects)
        contributes 25.56 * kNormTenant2.
      with default invalidState and cost = 10000 * 4 (four broken constraints)
    + 100 * (53 + 42.024 * kNormPerScopeItem + 25.56 * kNormTenant2)
      */
      EXPECT_NEAR(
          40000 +
              100 * (53 + 42.024 * kNormPerScopeItem + 25.56 * kNormTenant2),
          initialObjectiveValue,
          1e-8);

      // it is possible to fix this constraint by moving objects to host6 which
      // is outside of region scope
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
    case OPTIMAL: {
      // same as above but without penalty terms
      // goal value without invalidState and invalidCost =
      // (40-16) + (24-16) + (28-13) + (16-10) = 24+8+15+6 = 53
      // with default invalidState and cost = 10000 * 4 (four broken
      // constraints) + 100*(53) = 45300
      EXPECT_NEAR(45300, initialObjectiveValue, 1e-8);
      EXPECT_NEAR(0.0, finalObjectiveValue, 1e-8);
      break;
    }
  }
}

// Demonstrates the penalty dominance problem with multiple tenants: the solver
// moves a high-util tenant's objects between regions to reduce the penalty,
// creating new violations that affect all tenants sharing the destination.
//
// Setup: PER_GROUP_AND_SCOPE_ITEM, 2 regions, SOFT constraint policy.
//   - tenantBig in region1: 10 objects each with val=100 => 1000 util,
//   limit=999, violation=1. No objects in region2 initially; has limit 3 in
//   that region.
//   - tenants t1-t4 in region2: each 1 object with val=4, limit=3, violation=1.
//
// With CONTINUOUS_UTILIZATION penalty (= raw continuousUtil = 1000):
//   Moving tenantBig's object (val=100) from region1 to region2:
//     tenantBig region1: penalty drops by 100 => no constraint violation in
//     region which in turns saves invalidCost*100 + invalidCost*penalty
//     + invalidCost = 10000 + 100 * 1000 + 100  =  110100
// tenantBig region2: creates violation = 100-3 = 97, penalty = 100
//       costs invalidCost*(97+100) + invalidState = 29700
//     Net: region1 saves 110100, region2 costs 29700 => net improvement 80400
//     The solver takes the move, creating a huge new violation in region2.
//
// With NORMALIZED_CONTINUOUS_UTILIZATION penalty
//   (= continuousUtil * 0.5 * min(minPositiveDimValue, 1) / maxPossibleUtil,
//    bounded to [0, 0.5 * min(minPositiveDimValue, 1)]):
//   minPositiveDimValue = 4, min(4, 1) = 1, so penalty is bounded to [0, 0.5].
//   Penalty contribution is tiny, violation cost dominates.
//   The solver correctly rejects the move.
TEST_P(
    CapacityWithGroupPresenceTest,
    PenaltyDominanceCausesViolationWorsening) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "This test is specific to local search penalty behavior";
  }

  solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("trafficObject");
  solver->setContainerName("host");

  constexpr int kNumBigObjects = 10;
  std::vector<std::string> bigObjects;
  bigObjects.reserve(kNumBigObjects);
  for (auto i : folly::irange(1, kNumBigObjects + 1)) {
    bigObjects.push_back(fmt::format("big{}", i));
  }

  // host1 (region1): tenantBig's 10 objects each with val=100
  // host2 (region2): 4 small tenants, each 1 object with val=4
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host1", bigObjects},
          {"host2", {"t1o1", "t2o1", "t3o1", "t4o1"}},
      });

  solver->addScope(
      "region",
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"region1", {"host1"}}, {"region2", {"host2"}}});

  folly::F14FastMap<std::string, double> replicaCounts;
  for (const auto& obj : bigObjects) {
    replicaCounts[obj] = 100;
  }
  for (const auto& obj : {"t1o1", "t2o1", "t3o1", "t4o1"}) {
    replicaCounts[obj] = 4;
  }
  solver->addObjectDimension("replica_count", replicaCounts);

  solver->addPartition(
      "tenant",
      std::map<std::string, std::vector<std::string>>{
          {"tenantBig", bigObjects},
          {"t1", {"t1o1"}},
          {"t2", {"t2o1"}},
          {"t3", {"t3o1"}},
          {"t4", {"t4o1"}},
      });

  CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replica_count";
  spec.partition() = "tenant";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.bound() = CapacityWithGroupPresenceBound::MAX;
  spec.intent() =
      CapacityWithGroupPresenceUsageIntent::PER_GROUP_AND_SCOPE_ITEM;
  spec.continuousPenaltyType() =
      ContinuousPenaltyType::NORMALIZED_CONTINUOUS_UTILIZATION;

  // tenantBig: limit=999 in region1 (violation=1), limit=3 in region2
  // t1-t4: limit=3 in each region (each has violation=1 in region2)
  auto& capacityLimits = *spec.scopeItemToLimit();
  capacityLimits.type() = interface::LimitType::ABSOLUTE;
  capacityLimits.globalLimit() = 3;
  capacityLimits.scopeItemToGroupLimits() = {
      {"region1", {{"tenantBig", 999}}},
  };

  // SOFT policy: violations can worsen if penalty reduction outweighs cost
  solver->addConstraint(
      spec,
      ConstraintPolicy::SOFT,
      /*invalidCost=*/100,
      /*invalidState=*/10000);

  auto lsSpec = LocalSearchSolverSpec();
  lsSpec.moveTypeList() = {
      ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec{})};
  solver->addSolver(lsSpec);

  const auto solution = solver->solve();
  const auto& finalAssignment = *solution.assignment();

  // No tenantBig objects should move to region2. Each such move creates a
  // violation of ~97 (val=100 vs limit=3) in region2 while only reducing
  // the penalty in region1. With 4 small tenants already in region2, the
  // solver would be worsening the shared region to chase one tenant's penalty.
  int bigMovedToRegion2 = 0;
  for (const auto& obj : bigObjects) {
    if (finalAssignment.at(obj) == "host2") {
      bigMovedToRegion2++;
    }
  }

  EXPECT_EQ(0, bigMovedToRegion2)
      << bigMovedToRegion2
      << " tenantBig objects moved from region1 to region2, creating new "
         "violations (val=100 >> limit=3). The penalty (= raw continuousUtil) "
         "makes this look like an improvement, but it worsens constraint "
         "health for all tenants sharing region2.";
}

// Demonstrates penalty-driven misordering of hot containers.
//
// Setup: PER_SCOPE_ITEM, 2 regions, 1 free host outside, SOFT policy.
//   - region1 (host1): 5 objects each with val=100 => 500 util, limit=499,
//   violation=1
//   - region2 (host2): 1 object each with val=12 => 12 util, limit=2,
//   violation=10
//   - host3: free host outside any region
//   - stopAfterMoves=1: forces the solver to pick one region to fix first
//
// With CONTINUOUS_UTILIZATION: region1's softComponent ≈ 60100 (penalty=500
// inflates it) vs region2's ≈ 12200 (penalty=12). Solver explores region1
// first (hotter), fixes it, leaves region2 (violation=10) broken.
//
// With NORMALIZED_CONTINUOUS_UTILIZATION
//   (= continuousUtil * 0.5 * min(minPositiveDimValue, 1) / maxPossibleUtil):
//   minPositiveDimValue = 12, min(12, 1) = 1, maxPossibleUtil = 6*100 = 600.
//   region1's softComponent ≈ 10000 + 100*(1 + 500*0.5/600)
//   vs region2's ≈ 10000 + 100*(10 + 12*0.5/600).
//   Solver correctly explores region2 first (larger violation), fixes it.
TEST_P(CapacityWithGroupPresenceTest, PenaltyInflationMisordersHotContainers) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "This test is specific to local search penalty behavior";
  }

  solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("trafficObject");
  solver->setContainerName("host");

  std::vector<std::string> region1Objects;
  region1Objects.reserve(5);
  for (auto i : folly::irange(1, 6)) {
    region1Objects.push_back(fmt::format("a{}", i));
  }

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host1", region1Objects},
          {"host2", {"b1"}},
          {"host3", {}},
      });

  solver->addScope(
      "region",
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"region1", {"host1"}}, {"region2", {"host2"}}});

  folly::F14FastMap<std::string, double> utilValues;
  for (const auto& obj : region1Objects) {
    utilValues[obj] = 100;
  }
  utilValues["b1"] = 12;
  solver->addObjectDimension("util", utilValues);

  auto allObjects = region1Objects;
  allObjects.emplace_back("b1");
  solver->addPartition(
      "tenant",
      std::map<std::string, std::vector<std::string>>{
          {"tenantA", std::move(allObjects)},
      });

  CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "util";
  spec.partition() = "tenant";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.bound() = CapacityWithGroupPresenceBound::MAX;
  spec.intent() = CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM;
  spec.continuousPenaltyType() =
      ContinuousPenaltyType::NORMALIZED_CONTINUOUS_UTILIZATION;

  auto& capacityLimits = *spec.scopeItemToLimit();
  capacityLimits.type() = interface::LimitType::ABSOLUTE;
  capacityLimits.globalLimit() = 2;
  capacityLimits.scopeItemLimits() = {{"region1", 499}};

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.type() = interface::LimitType::ABSOLUTE;
  groupToPresenceWeight.globalLimit() = 1;

  solver->addConstraint(
      spec,
      ConstraintPolicy::SOFT,
      /*invalidCost=*/100,
      /*invalidState=*/10000);

  auto lsSpec = LocalSearchSolverSpec();
  lsSpec.moveTypeList() = {
      ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec{})};
  lsSpec.stopAfterMoves() = 1;
  solver->addSolver(lsSpec);

  const auto solution = solver->solve();
  const auto& initialAssignment = *solution.initialAssignment();
  const auto& finalAssignment = *solution.assignment();

  // b1 should move (fixing the more violated region2)
  const bool b1Moved = finalAssignment.at("b1") != initialAssignment.at("b1");

  // No region1 objects should move
  int region1Moves = 0;
  for (const auto& obj : region1Objects) {
    if (finalAssignment.at(obj) != initialAssignment.at(obj)) {
      region1Moves++;
    }
  }

  EXPECT_TRUE(b1Moved)
      << "b1 (region2, violation=10) should be moved first. If not moved, the "
         "penalty for region1 (continuousUtil=500) is inflating region1's "
         "priority over region2 (violation=10 > violation=1).";
  EXPECT_EQ(0, region1Moves)
      << "Region1 objects should not move; region2 (violation=10) should be "
         "prioritized over region1 (violation=1).";
}

} // namespace facebook::rebalancer::interface::tests
