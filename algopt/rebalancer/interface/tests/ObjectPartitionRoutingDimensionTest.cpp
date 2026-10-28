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

namespace facebook::rebalancer::interface::tests {

class ObjectPartitionRoutingDimensionTest
    : public ::testing::TestWithParam<int> {
 protected:
  std::vector<std::vector<std::string>> routingLogic1 = {
      {"region1"},
      {"region0", "region2"}};
  std::vector<std::vector<std::string>> routingLogic2 = {
      {"region0", "region1", "region2"}};
  std::vector<std::vector<std::string>> routingLogic3 = {
      {"region2"},
      {"region0", "region1"}};

  static RoutingRing getRoutingRing(
      const std::string& origin,
      double originTraffic,
      const std::optional<std::vector<std::vector<std::string>>>&
          destinationScopeItemSets) {
    RoutingRing ring;
    ring.originScopeItem() = origin;
    ring.originTraffic() = originTraffic;
    if (destinationScopeItemSets.has_value()) {
      ring.destinationScopeItemSets() = destinationScopeItemSets.value();
    }

    return ring;
  }

  std::unordered_map<std::string, GroupRoutingRings> getGroupToRoutingRings(
      bool useDefaultRoutingLogic) {
    std::unordered_map<std::string, GroupRoutingRings> groupToRoutingRings;

    constexpr double totalTrafficUnitsPerTenant = 100;
    // 98% of the traffic for tenant0 originates in region0, 2% at region1,
    GroupRoutingRings tenant0Rings;
    tenant0Rings.routingRings()->push_back(getRoutingRing(
        "region0", // origin
        0.98 * totalTrafficUnitsPerTenant, // originTraffic
        useDefaultRoutingLogic
            ? std::nullopt
            : std::make_optional(routingLogic1) // routingLogic
        ));

    tenant0Rings.routingRings()->push_back(getRoutingRing(
        "region1", // origin
        0.02 * totalTrafficUnitsPerTenant, // originTraffic
        useDefaultRoutingLogic
            ? std::nullopt
            : std::make_optional(routingLogic2) // routingLogic
        ));

    groupToRoutingRings.emplace("tenant0", tenant0Rings);

    // 100% of the traffic for tenant1 originates in region2.
    GroupRoutingRings tenant1Rings;
    tenant1Rings.routingRings()->push_back(getRoutingRing(
        "region2", // origin
        totalTrafficUnitsPerTenant, // originTraffic
        useDefaultRoutingLogic
            ? std::nullopt
            : std::make_optional(routingLogic3) // routingLogic
        ));

    groupToRoutingRings.emplace("tenant1", tenant1Rings);

    return groupToRoutingRings;
  }

  std::unordered_map<std::string, std::vector<std::vector<std::string>>>
  getDefaultOriginToDestinationScopeItemSets(bool useDefaultRoutingLogic) {
    std::unordered_map<std::string, std::vector<std::vector<std::string>>>
        defaultMap;
    if (useDefaultRoutingLogic) {
      defaultMap.emplace("region0", routingLogic1);
      defaultMap.emplace("region1", routingLogic2);
      defaultMap.emplace("region2", routingLogic3);
    }
    return defaultMap;
  }

  static std::
      unordered_map<std::string, std::unordered_map<std::string, double>>
      getOriginToDestinationLatency() {
    return std::
        unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"region0", {{"region0", 5}, {"region1", 10}, {"region2", 5}}},
            {"region1", {{"region0", 5}, {"region1", 0}, {"region2", 15}}},
            {"region2", {{"region0", 50}, {"region1", 40}, {"region2", 0}}}};
  }

  static void verifyTrafficMetrics(
      const std::string& routingConfigName,
      const AssignmentSolution& solution,
      const std::vector<std::tuple<
          std::string /*tenant*/,
          std::string /*sourceRegion*/,
          std::string /*destinationRegion*/,
          double>>& expectedInitialTenantRegionTraffic,
      const std::vector<
          std::tuple<std::string, std::string, std::string, double>>&
          expectedFinalTenantRegionTraffic) {
    auto& actualInitialTrafficMetrics =
        solution.initialMetrics()->routingConfigToGroupTrafficMetrics()->at(
            routingConfigName);
    auto& actualFinalTrafficMetrics =
        solution.finalMetrics()->routingConfigToGroupTrafficMetrics()->at(
            routingConfigName);

    auto assertEqual = [&](const auto& actualTrafficMetrics,
                           const auto& expectedTrafficMetrics) {
      for (auto& [tenant, sourceRegion, destinationRegion, traffic] :
           expectedTrafficMetrics) {
        auto& tenantSourceSummary = actualTrafficMetrics.groupToSourceTraffic()
                                        ->at(tenant)
                                        .sourceToDestinationTraffic()
                                        ->at(sourceRegion);

        // not all destinations maybe part of the map associated with a
        // source; if a destination is not present, then traffic to it is 0
        auto value = folly::get_default(
            *tenantSourceSummary.scopeItemToValue(), destinationRegion, 0.0);
        EXPECT_NEAR(value, traffic, 1e-8);
      }
    };

    assertEqual(
        actualInitialTrafficMetrics, expectedInitialTenantRegionTraffic);
    assertEqual(actualFinalTrafficMetrics, expectedFinalTenantRegionTraffic);
  }

  static void verifyScopeItemAfterUtilMetrics(
      const std::string& dimensionName,
      const std::string& scopeName,
      const AssignmentSolution& solution,
      const std::vector<std::pair<std::string, double>>&
          expectedInitialRegionLoadPairs,
      const std::vector<std::pair<std::string, double>>&
          expectedFinalRegionLoadPairs) {
    verifyScopeItemUtilMetrics(
        dimensionName,
        scopeName,
        solution,
        expectedInitialRegionLoadPairs,
        expectedFinalRegionLoadPairs,
        "after");
  }

  static void verifyScopeItemDuringUtilMetrics(
      const std::string& dimensionName,
      const std::string& scopeName,
      const AssignmentSolution& solution,
      const std::vector<std::pair<std::string, double>>&
          expectedInitialRegionLoadPairs,
      const std::vector<std::pair<std::string, double>>&
          expectedFinalRegionLoadPairs) {
    verifyScopeItemUtilMetrics(
        dimensionName,
        scopeName,
        solution,
        expectedInitialRegionLoadPairs,
        expectedFinalRegionLoadPairs,
        "during");
  }

  static void verifyScopeItemUtilMetrics(
      const std::string& dimensionName,
      const std::string& scopeName,
      const AssignmentSolution& solution,
      const std::vector<std::pair<std::string, double>>&
          expectedInitialRegionLoadPairs,
      const std::vector<std::pair<std::string, double>>&
          expectedFinalRegionLoadPairs,
      const std::string& utilMetric) {
    auto& actualInitialUtilMetrics = solution.initialMetrics()
                                         ->utilMetricToScopeUtils()
                                         ->at(utilMetric)
                                         .scopeToDimensionUtils()
                                         ->at(scopeName);
    auto& actualFinalUtilMetrics = solution.finalMetrics()
                                       ->utilMetricToScopeUtils()
                                       ->at(utilMetric)
                                       .scopeToDimensionUtils()
                                       ->at(scopeName);

    auto assertEqual = [&](const auto& actualUtilMetrics,
                           const auto& expectedUtilMetrics) {
      for (auto& [region, load] : expectedUtilMetrics) {
        auto value = actualUtilMetrics.dimensionToScopeItemUtils()
                         ->at(dimensionName)
                         .scopeItemToValue()
                         ->at(region);

        EXPECT_NEAR(value, load, 1e-8);
      }
    };

    assertEqual(actualInitialUtilMetrics, expectedInitialRegionLoadPairs);
    assertEqual(actualFinalUtilMetrics, expectedFinalRegionLoadPairs);
  }

  static void verifyScopeItemGroupUtilMetrics(
      const std::string& dimensionName,
      const std::string& scopeName,
      const AssignmentSolution& solution,
      const std::vector<
          std::tuple<std::string, std::string, std::string, double>>&
          expectedInitialMetrics,
      const std::vector<
          std::tuple<std::string, std::string, std::string, double>>&
          expectedFinalMetrics,
      const std::string& utilMetric = "after") {
    auto& actualInitialUtilMetrics = solution.initialMetrics()
                                         ->utilMetricToScopeUtils()
                                         ->at(utilMetric)
                                         .scopeToDimensionUtils()
                                         ->at(scopeName);
    auto& actualFinalUtilMetrics = solution.finalMetrics()
                                       ->utilMetricToScopeUtils()
                                       ->at(utilMetric)
                                       .scopeToDimensionUtils()
                                       ->at(scopeName);

    auto assertEqual = [&](const auto& actualUtilMetrics,
                           const auto& expectedUtilMetrics) {
      for (auto& [scopeItem, partition, group, expectedValue] :
           expectedUtilMetrics) {
        auto& dimensionUtils =
            actualUtilMetrics.dimensionToScopeItemUtils()->at(dimensionName);
        EXPECT_TRUE(
            dimensionUtils.scopeItemToPartitionUtils()->contains(scopeItem))
            << "scopeItem " << scopeItem
            << " not found in scopeItemToPartitionUtils";

        auto& partitionUtils =
            dimensionUtils.scopeItemToPartitionUtils()->at(scopeItem);
        auto& groupUtils =
            partitionUtils.partitionToGroupUtils()->at(partition);
        auto actualValue = groupUtils.groupToValue()->at(group);
        EXPECT_NEAR(actualValue, expectedValue, 1e-8)
            << "Value mismatch for scopeItem=" << scopeItem
            << ", partition=" << partition << ", group=" << group
            << " expectedValue=" << expectedValue
            << " actualValue=" << actualValue;
      }
    };

    assertEqual(actualInitialUtilMetrics, expectedInitialMetrics);
    assertEqual(actualFinalUtilMetrics, expectedFinalMetrics);
  }

  void setUpBasicProblem(
      bool useDefaultRoutingLogic,
      const std::vector<std::pair<std::string, std::vector<std::string>>>&
          initialAssignment =
              {
                  {"region0", {"replica1:0"}},
                  {"region1", {"replica0:0", "replica1:1"}},
                  {"region2", {"replica0:1"}},
              },
      const std::unordered_map<std::string, double>& groupToStaticValue = {},
      const double defaultStaticValue = 0) {
    // Simulate 2 tenants with 2 replicas each.
    solver_ = initializeTestProblemSolver({.executorThreadCount = GetParam()});
    solver_->setObjectName("replica");
    solver_->setContainerName("region");

    solver_->setAssignment(initialAssignment);

    // Group replicas of the same tenant together in a partition.
    {
      replicaToTenant_ = {
          {"replica0:0", "tenant0"},
          {"replica0:1", "tenant0"},
          {"replica1:0", "tenant1"},
          {"replica1:1", "tenant1"},
      };
      solver_->addPartition("tenant", replicaToTenant_);
    }

    // define routingConfig
    {
      solver_->addRoutingConfig(
          "routingConfig1",
          "region",
          "tenant",
          getGroupToRoutingRings(useDefaultRoutingLogic),
          getOriginToDestinationLatency(),
          getDefaultOriginToDestinationScopeItemSets(useDefaultRoutingLogic));
    }

    // define a ObjectPartitionRoutingDimension "load"
    {
      solver_->addObjectPartitionRoutingDimension(
          "load" /* dimensionName */,
          "tenant" /* partitionName */,
          "routingConfig1" /* routingConfigName */,
          std::unordered_map<std::string, double>{
              {"tenant0", 20},
              {"tenant1", 50}} /* tenant to base 'load' value*/,
          groupToStaticValue /* group to static value */,
          0 /* default 'load' value */,
          defaultStaticValue /* default static value */);
    }

    // Constraint: At most one replica of the same tenant can be placed in the
    // same region.
    {
      GroupCountSpec spec;
      spec.scope() = "region";
      spec.partitionName() = "tenant";
      spec.dimension() = "replica_count";
      spec.limit()->globalLimit() = 1;
      solver_->addConstraint(spec);
    }

    solver_->addSolver(makeDefaultLocalSearchSolver());

    // publish metrics like container utilization to AssignmentSolution
    solver_->publishMetrics();
  }

  std::unique_ptr<ProblemSolver> solver_;
  std::map<std::string, std::string> replicaToTenant_;
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    ObjectPartitionRoutingDimensionTest,
    testThreadCounts());

TEST_P(ObjectPartitionRoutingDimensionTest, CapacityWithAbsoluteLimit) {
  setUpBasicProblem(false);

  // Constraint: Limit the tenant load at every region to 35
  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 35.0;

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();

  // In the initial solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 98% goes to region1
  //    - 2% of traffic is from region1
  //      -- 1% goes to region1, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region
  //    region0: total load = 25
  //      - region0 gets 0% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region0 = 0*20 + 0.5*50 = 25
  //
  //    region1: total load = 44.8
  //      - region1 gets 99% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region1 = 0.99*20 + 0.5*50 = 44.8
  //
  //    region2: total load = 0.2
  //      - region2 gets 1% of tenant0 's traffic, 0% of tenant1' s traffic
  //        => total load on region2 = 0.01*20 + 0.0*50 = 0.2

  // initial objective value (capacity limit is 35) = 0 + (44.8 - 35) + 0 = 9.8
  EXPECT_NEAR(9.8, *solution.initialObjective()->value(), 1e-8);

  // In the expected best solution: move a replica of tenant0 from region1 to
  // region0. This enables 98% from region0 of traffic to be evenly spread
  // across region0 and region2. Also, after this 2% traffic from
  // region 1 is equally spread to region0 and region1.

  // In the final solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 49% goes to region0, 49% to region2
  //    - 2% of traffic is from region1
  //      -- 1% goes to region0, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region
  //    region0: total load = 35
  //      - region0 gets 50% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region0 = 0.5*20 + 0.5*50 = 35
  //
  //    region1: total load = 25
  //      - region1 gets 0% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region1 = 0.0*20 + 0.5*50 = 25
  //
  //    region2: total load = 10
  //      - region2 gets 50% of tenant0 's traffic, 0% of tenant1' s traffic
  //        => total load on region2 = 0.5*20 + 0.0*50 = 10

  // final objective value (capacity limit is 35) = 0 + 0 + 0
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // we expect a replica of tenant0 to have moved from region1 to region0
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);

  // we expect that region2 has no replica of tenant1 (since if so, then all of
  // tenant1's traffic will be sent there, which in turn would result in a total
  // load of at least 50)
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant1"]);

  // verify scopeItem utils w.r.t. "load" (for explanation see above)
  verifyScopeItemAfterUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 25.0},
       {"region1", 44.8},
       {"region2", 0.2}}, // initial metrics
      {{"region0", 35.0},
       {"region1", 25.0},
       {"region2", 10.0}}); // final metrics

  // Verify group scope item util metrics
  verifyScopeItemGroupUtilMetrics(
      "load",
      "region",
      solution,
      {
          // Initial metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 0.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 19.8},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 0.2},
          {"region2", "tenant", "tenant1", 0.0},
      },
      {
          // Final metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 10.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 0.0},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 10.0},
          {"region2", "tenant", "tenant1", 0.0},
      });

  // verify initial and final routingTrafficMetrics (for explanation see above)
  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.49},
          {"tenant0", "region0", "region1", 0.0},
          {"tenant0", "region0", "region2", 0.49},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.0},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

TEST_P(ObjectPartitionRoutingDimensionTest, CapacityWithAbsoluteLimitTwoMoves) {
  setUpBasicProblem(
      false,
      {
          {"region0", {"replica1:0"}},
          {"region1", {"replica0:0"}},
          {"region2", {"replica0:1", "replica1:1"}},
      });

  // Constraint: Limit the tenant load at every region to 35
  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 35.0;

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();

  // In the initial solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 98% goes to region1
  //    - 2% of traffic is from region1
  //      -- 1% goes to region1, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 100% goes to region2

  // 'Load' on each region
  //    region0: total load = 0
  //      - region0 gets 0% of tenant0's traffic, 0% of tenant1's traffic
  //        => total load on region0 = 0*20 + 0*50 = 0
  //
  //    region1: total load = 19.8
  //      - region1 gets 99% of tenant0's traffic, 0% of tenant1's traffic
  //        => total load on region1 = 0.99*20 + 0*50 = 19.8
  //
  //    region2: total load = 50.2
  //      - region2 gets 1% of tenant0 's traffic, 1000% of tenant1' s traffic
  //        => total load on region2 = 0.01*20 + 1*50 = 50.2

  // initial objective value (capacity limit is 35) = 0 + 0 + (50.2 - 35) = 15.2
  EXPECT_NEAR(15.2, *solution.initialObjective()->value(), 1e-8);

  // In the expected best solution: move a replica of tenant1 from region2 to
  // region 1 and then move a tenant0 from region1 to region0. This enables 98%
  // from region0 of traffic to be evenly spread across region0 and region2.
  // Also, after this 2% traffic from region 1 is equally spread to region0 and
  // region1.

  // In the final solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 49% goes to region0, 49% to region2
  //    - 2% of traffic is from region1
  //      -- 1% goes to region0, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region
  //    region0: total load = 35
  //      - region0 gets 50% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region0 = 0.5*20 + 0.5*50 = 35
  //
  //    region1: total load = 25
  //      - region1 gets 0% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region1 = 0.0*20 + 0.5*50 = 25
  //
  //    region2: total load = 10
  //      - region2 gets 50% of tenant0 's traffic, 0% of tenant1' s traffic
  //        => total load on region2 = 0.5*20 + 0.0*50 = 10

  // final objective value (capacity limit is 35) = 0 + 0 + 0
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // we expect a replica of tenant0 to have moved from region1 to region0
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);

  // we expect that region2 has no replica of tenant1 (since if so, then all of
  // tenant1's traffic will be sent there, which in turn would result in a total
  // load of at least 50)
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant1"]);

  // verify scopeItem utils w.r.t. "load" (for explanation see above)
  verifyScopeItemAfterUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 0}, {"region1", 19.8}, {"region2", 50.2}}, // initial metrics
      {{"region0", 35.0},
       {"region1", 25.0},
       {"region2", 10.0}}); // final metrics

  // Verify group scope item util metrics
  verifyScopeItemGroupUtilMetrics(
      "load",
      "region",
      solution,
      {
          // Initial metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 0.0},
          {"region0", "tenant", "tenant1", 0.0},
          {"region1", "tenant", "tenant0", 19.8},
          {"region1", "tenant", "tenant1", 0.0},
          {"region2", "tenant", "tenant0", 0.2},
          {"region2", "tenant", "tenant1", 50.0},
      },
      {
          // Final metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 10.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 0.0},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 10.0},
          {"region2", "tenant", "tenant1", 0.0},
      });

  // verify initial and final routingTrafficMetrics (for explanation see above)
  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.0},
          {"tenant1", "region2", "region1", 0.0},
          {"tenant1", "region2", "region2", 1.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.49},
          {"tenant0", "region0", "region1", 0.0},
          {"tenant0", "region0", "region2", 0.49},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.0},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

TEST_P(
    ObjectPartitionRoutingDimensionTest,
    CapacityWithAbsoluteLimitDuringAndAfter) {
  setUpBasicProblem(
      false,
      {
          {"region0", {"replica1:0"}},
          {"region1", {"replica0:0"}},
          {"region2", {"replica0:1", "replica1:1"}},
      });

  // Constraint: Limit the tenant load at every region to 35
  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::DURING_AND_AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 35.0;

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();

  // In the initial solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 98% goes to region1
  //    - 2% of traffic is from region1
  //      -- 1% goes to region1, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 100% goes to region2

  // 'Load' on each region
  //    region0: total load = 0
  //      - region0 gets 0% of tenant0's traffic, 0% of tenant1's traffic
  //        => total load on region0 = 0*20 + 0*50 = 0
  //
  //    region1: total load = 19.8
  //      - region1 gets 99% of tenant0's traffic, 0% of tenant1's traffic
  //        => total load on region1 = 0.99*20 + 0*50 = 19.8
  //
  //    region2: total load = 50.2
  //      - region2 gets 1% of tenant0 's traffic, 1000% of tenant1' s traffic
  //        => total load on region2 = 0.01*20 + 1*50 = 50.2

  // initial objective values (capacity limit is 35):
  //   AFTER value = 0 + 0 + (50.2 - 35) = 15.2
  //   DURING value = same as AFTER initially
  //   TOTAL value = 15.2 + 15.2 = 30.4
  EXPECT_NEAR(30.4, *solution.initialObjective()->value(), 1e-8);

  // Because of the DURING constraint, the only move that improves the total
  // objective is to move a replica of tenant0 from region2 to region0. This
  // slightly reduces the load on region2 without violating the DURING
  // constraint on region0 or region1.

  // In the final solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 98% goes to region1
  //    - 2% of traffic is from region1
  //      -- 1% goes to region0, 1% to region1
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 100% goes to region2

  // 'Load' on each region
  //    region0: total load = 0.2
  //      - region0 gets 1% of tenant0's traffic, 0% of tenant1's traffic
  //        => total load on region0 = 0.01*20 + 0*50 = 0.2
  //
  //    region1: total load = 19.8
  //      - region1 gets 99% of tenant0's traffic, 0% of tenant1's traffic
  //        => total load on region1 = 0.99*20 + 0*50 = 19.8
  //
  //    region2: total load = 50.0
  //      - region2 gets 0% of tenant0 's traffic, 1000% of tenant1' s traffic
  //        => total load on region2 = 0*20 + 1*50 = 50

  // final objective values (capacity limit is 35):
  //   AFTER value = 0 + 0 + (50 - 35) = 15.0
  //   DURING value = MAX(initial, final) = 0 + 0 + (max(50, 50.2) - 35) = 15.2
  //   TOTAL value = 15.0 + 15.2 = 30.2
  EXPECT_NEAR(30.2, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // we expect a replica of tenant0 to have moved from region2 to region0
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);
  EXPECT_EQ(1, regionTenantCount["region1"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region1"]["tenant1"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant1"]);

  // verify scopeItem utils w.r.t. "load" (for explanation see above)
  verifyScopeItemDuringUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 0}, {"region1", 19.8}, {"region2", 50.2}}, // initial metrics
      {{"region0", 0.2},
       {"region1", 19.8},
       {"region2", 50.2}}); // final metrics

  // Verify group scope item util metrics
  verifyScopeItemGroupUtilMetrics(
      "load",
      "region",
      solution,
      {
          // Initial metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 0.0},
          {"region0", "tenant", "tenant1", 0.0},
          {"region1", "tenant", "tenant0", 19.8},
          {"region1", "tenant", "tenant1", 0.0},
          {"region2", "tenant", "tenant0", 0.2},
          {"region2", "tenant", "tenant1", 50.0},
      },
      {
          // Final metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 0.2},
          {"region0", "tenant", "tenant1", 0.0},
          {"region1", "tenant", "tenant0", 19.8},
          {"region1", "tenant", "tenant1", 0.0},
          {"region2", "tenant", "tenant0", 0.2},
          {"region2", "tenant", "tenant1", 50.0},
      },
      "during");

  // verify initial and final routingTrafficMetrics (for explanation see above)
  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.0},
          {"tenant1", "region2", "region1", 0.0},
          {"tenant1", "region2", "region2", 1.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.0},
          //
          {"tenant1", "region2", "region0", 0.0},
          {"tenant1", "region2", "region1", 0.0},
          {"tenant1", "region2", "region2", 1.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

TEST_P(ObjectPartitionRoutingDimensionTest, CapacityWithRelativeLimit) {
  setUpBasicProblem(false);

  // add a scopeDimension "load"
  solver_->addScopeDimension(
      "load",
      "region",
      std::unordered_map<std::string, double>{
          {"region0", 70.0}, {"region1", 35.0}, {"region2", 35.0}});

  // Constraint: Limit the tenant load at every region to relative limit of (35
  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::RELATIVE;
    limit.globalLimit() = 1.0;
    limit.scopeItemLimits()->emplace(
        "region0",
        0.5); // 0.5 since we still want to max absolute load to be at most 35.0

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();

  // In the initial solution: follow the exact same calculations as in the test
  // case 'CapacityWithAbsoluteLimit'. The only difference to the initial
  // objective value comes from a multiplication by (3/140), which is
  // 1/avgCapacity (i.e., 1/ (70 + 35 + 35)). This is the normalization factor
  // used in CapacitySpec.

  // initial objective value = 3/140 * (0 + (44.8 - 35) + 0) = 3/140 * 9.8
  EXPECT_NEAR(0.21, *solution.initialObjective()->value(), 1e-8);

  // In the final solution: follow the exact same calculations as in the test
  // case 'CapacityWithAbsoluteLimit'.
  // final objective value (capacity limit is 35) = 3/140 * (0 + 0 + 0) = 0.0
  EXPECT_NEAR(0.0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // we expect a replica of tenant0 to have moved from region1 to region0
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);

  // we expect that region2 has no replica of tenant1 (since if so, then all of
  // tenant1's traffic will be sent there, which in turn would result in a total
  // load of at least 50)
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant1"]);

  // verify scopeItem utils w.r.t. "load"; Note that these are still absolute
  // utils
  verifyScopeItemAfterUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 25.0},
       {"region1", 44.8},
       {"region2", 0.2}}, // initial metrics
      {{"region0", 35.0},
       {"region1", 25.0},
       {"region2", 10.0}}); // final metrics

  // Verify group scope item util metrics
  verifyScopeItemGroupUtilMetrics(
      "load",
      "region",
      solution,
      {
          // Initial metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 0.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 19.8},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 0.2},
          {"region2", "tenant", "tenant1", 0.0},
      },
      {
          // Final metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 10.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 0.0},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 10.0},
          {"region2", "tenant", "tenant1", 0.0},
      });

  // verify initial and final routingTrafficMetrics (for explanation see above)
  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.49},
          {"tenant0", "region0", "region1", 0.0},
          {"tenant0", "region0", "region2", 0.49},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.0},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

TEST_P(
    ObjectPartitionRoutingDimensionTest,
    CapacityWithAbsoluteLimitUsingDefaultDestinationScopeItemSetsBasic) {
  setUpBasicProblem(true);
  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 35.0;

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();
  EXPECT_NEAR(9.8, *solution.initialObjective()->value(), 1e-8);
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant1"]);

  verifyScopeItemAfterUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 25.0},
       {"region1", 44.8},
       {"region2", 0.2}}, // initial metrics
      {{"region0", 35.0},
       {"region1", 25.0},
       {"region2", 10.0}}); // final metrics

  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.49},
          {"tenant0", "region0", "region1", 0.0},
          {"tenant0", "region0", "region2", 0.49},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.0},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

TEST_P(
    ObjectPartitionRoutingDimensionTest,
    CapacityWithRelativeLimitUsingDefaultDestinationScopeItemSetsBasic) {
  setUpBasicProblem(true);
  solver_->addScopeDimension(
      "load",
      "region",
      std::unordered_map<std::string, double>{
          {"region0", 70.0}, {"region1", 35.0}, {"region2", 35.0}});

  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::RELATIVE;
    limit.globalLimit() = 1.0;
    limit.scopeItemLimits()->emplace(
        "region0",
        0.5); // 0.5 since we still want to max absolute load to be at most 35.0

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();
  EXPECT_NEAR(0.21, *solution.initialObjective()->value(), 1e-8);

  EXPECT_NEAR(0.0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // we expect a replica of tenant0 to have moved from region1 to region0
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);

  // we expect that region2 has no replica of tenant1 (since if so, then all of
  // tenant1's traffic will be sent there, which in turn would result in a total
  // load of at least 50)
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant1"]);

  // verify scopeItem utils w.r.t. "load"; Note that these are still absolute
  // utils
  verifyScopeItemAfterUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 25.0},
       {"region1", 44.8},
       {"region2", 0.2}}, // initial metrics
      {{"region0", 35.0},
       {"region1", 25.0},
       {"region2", 10.0}}); // final metrics

  // verify initial and final routingTrafficMetrics (for explanation see above)
  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.49},
          {"tenant0", "region0", "region1", 0.0},
          {"tenant0", "region0", "region2", 0.49},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.0},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

TEST_P(
    ObjectPartitionRoutingDimensionTest,
    CapacityWithGroupPresenceAbsoluteLimit) {
  /*This is the same test as CapacityWithAbsoluteLimit with the main difference
   * being that here we use CapacityWithGroupPresenceSpec; this is to test the
   * getAbsolute(...) function calls work for a specific group*/
  setUpBasicProblem(false);

  // Constraint: Limit the tenant load at every region to 35
  {
    CapacityWithGroupPresenceSpec spec;
    spec.scope() = "region";
    spec.dimension() = "load";
    spec.partition() = "tenant";
    spec.bound() = CapacityWithGroupPresenceBound::MAX;
    spec.roundUpGroupUtilOnScopeItem() = false;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 35.0;

    spec.scopeItemToLimit() = limit;

    solver_->addConstraint(
        spec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();

  // In the initial solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 98% goes to region1
  //    - 2% of traffic is from region1
  //      -- 1% goes to region1, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region
  //    region0: total load = 25
  //      - region0 gets 0% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region0 = 0*20 + 0.5*50 = 25
  //
  //    region1: total load = 44.8
  //      - region1 gets 99% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region1 = 0.99*20 + 0.5*50 = 44.8
  //
  //    region2: total load = 0.2
  //      - region2 gets 1% of tenant0 's traffic, 0% of tenant1' s traffic
  //        => total load on region2 = 0.01*20 + 0.0*50 = 0.2

  // initial objective value (capacity limit is 35) = 0 + (44.8 - 35) + 0 + 44.8
  // = 9.8 + 44.8 (raw penalty). NORMALIZED penalty (default) scales the raw
  // 44.8 by penaltyBound / (numObjects * maxDimValue) = 0.5/(4*50) = 1/400.
  // Separated penalty objectives report only the 9.8 violation.
  EXPECT_NEAR(
      expectedLocalSearchGlobalObjective(9.8, 44.8 / 400.0),
      *solution.initialObjective()->value(),
      1e-8);

  // In the expected best solution: move a replica of tenant0 from region1 to
  // region0. This enables 98% from region0 of traffic to be evenly spread
  // across region0 and region2. Also, after this 2% traffic from
  // region 1 is equally spread to region0 and region2.

  // In the final solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 49% goes to region0, 49% to region2
  //    - 2% of traffic is from region1
  //      -- 1% goes to region0, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region
  //    region0: total load = 35
  //      - region0 gets 50% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region0 = 0.5*20 + 0.5*50 = 35
  //
  //    region1: total load = 25
  //      - region1 gets 0% of tenant0's traffic, 50% of tenant1's traffic
  //        => total load on region1 = 0.0*20 + 0.5*50 = 25
  //
  //    region2: total load = 10
  //      - region2 gets 50% of tenant0 's traffic, 0% of tenant1' s traffic
  //        => total load on region2 = 0.5*20 + 0.0*50 = 10

  // final objective value (capacity limit is 35) = 0 + 0 + 0
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // we expect a replica of tenant0 to have moved from region1 to region0
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant0"]);
  EXPECT_EQ(1, regionTenantCount["region0"]["tenant1"]);

  // we expect that region2 has no replica of tenant1 (since if so, then all of
  // tenant1's traffic will be sent there, which in turn would result in a total
  // load of at least 50)
  EXPECT_EQ(1, regionTenantCount["region2"]["tenant0"]);
  EXPECT_EQ(0, regionTenantCount["region2"]["tenant1"]);

  // Verify group scope item util metrics
  verifyScopeItemGroupUtilMetrics(
      "load",
      "region",
      solution,
      {
          // Initial metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 0.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 19.8},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 0.2},
          {"region2", "tenant", "tenant1", 0.0},
      },
      {
          // Final metrics (scopeItem, partition, group, value)
          {"region0", "tenant", "tenant0", 10.0},
          {"region0", "tenant", "tenant1", 25.0},
          {"region1", "tenant", "tenant0", 0.0},
          {"region1", "tenant", "tenant1", 25.0},
          {"region2", "tenant", "tenant0", 10.0},
          {"region2", "tenant", "tenant1", 0.0},
      });
}

TEST_P(
    ObjectPartitionRoutingDimensionTest,
    CapacityWithAbsoluteLimitAndStaticValues) {
  const std::vector<std::pair<std::string, std::vector<std::string>>>
      initialAssignment = {
          {"region0", {"replica1:0"}},
          {"region1", {"replica0:0", "replica1:1"}},
          {"region2", {"replica0:1"}},
      };
  const std::unordered_map<std::string, double> groupToStaticValue = {
      {"tenant0", 5.0}};
  const double defaultStaticValue = 3.0;
  setUpBasicProblem(
      false, initialAssignment, groupToStaticValue, defaultStaticValue);

  // Constraint: Limit the tenant load at every region to 45
  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "region";
    capacitySpec.dimension() = "load";
    capacitySpec.definition() = CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = CapacitySpecBound::MAX;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 45.0;

    capacitySpec.limit() = limit;

    solver_->addConstraint(
        capacitySpec, std::nullopt, 1 /*invalidCost*/, 0 /*invalidState*/);
  }

  // Get a solution from Rebalancer.
  auto solution = solver_->solve();
  auto assignment = *solution.assignment();

  // With static values:
  // - tenant0 has static value 5.0, tenant1 has default static value 3.0

  // In the initial solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 98% goes to region1
  //    - 2% of traffic is from region1
  //      -- 1% goes to region1, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region (including static values):
  //    region0: total load = 25 + 3 (static) = 28
  //      - Dynamic: region0 gets 0% of tenant0's traffic, 50% of tenant1's
  //      traffic
  //        => dynamic load = 0*20 + 0.5*50 = 25
  //      - Static: region0 has tenant1 replica, so +3
  //
  //    region1: total load = 44.8 + 5.0 (static) + 3.0 (static) = 52.8
  //      - Dynamic: region1 gets 99% of tenant0's traffic, 50% of tenant1's
  //      traffic
  //        => dynamic load = 0.99*20 + 0.5*50 = 44.8
  //      - Static: region1 has both tenant0 and tenant1 replicas, so +5 + 3
  //
  //    region2: total load = 0.2 + 5 (static) = 5.2
  //      - Dynamic: region2 gets 1% of tenant0's traffic, 0% of tenant1's
  //      traffic
  //        => dynamic load = 0.01*20 + 0.0*50 = 0.2
  //      - Static: region2 has tenant0 replica, so +5

  // initial objective value (capacity limit is 45) = 0 + (52.8 - 45) + 0 = 7.8
  EXPECT_NEAR(7.8, *solution.initialObjective()->value(), 1e-8);

  // In the expected best solution: move a replica of tenant0 from region1 to
  // region0. This enables 98% from region0 of traffic to be evenly spread
  // across region0 and region2. Also, after this 2% traffic from
  // region 1 is equally spread to region0 and region1.

  // In the final solution:
  //  Tenant0:
  //    - 98% of traffic is from region0:
  //      -- 49% goes to region0, 49% to region2
  //    - 2% of traffic is from region1
  //      -- 1% goes to region0, 1% to region2
  //
  //  Tenant1:
  //    - 100% of traffic is from region2:
  //      -- 50% goes to region0, 50% to region1

  // 'Load' on each region (including static values):
  //    region0: total load = 35 + 5 (static) + 3 (static) = 43
  //      - Dynamic: region0 gets 50% of tenant0's traffic, 50% of tenant1's
  //      traffic
  //        => total load on region0 = 0.5*20 + 0.5*50 = 35
  //      - Static: region0 has both tenant0 and tenant1 replicas, so +5 + 3
  //
  //    region1: total load = 25 + 3 (static) = 28
  //      - Dynamic: region1 gets 0% of tenant0's traffic, 50% of tenant1's
  //      traffic
  //        => total load on region1 = 0.0*20 + 0.5*50 = 25
  //      - Static: region1 has tenant1 replica, so +3
  //
  //    region2: total load = 10 + 5 (static) = 15
  //      - Dynamic: region2 gets 50% of tenant0 's traffic, 0% of tenant1' s
  //      traffic
  //        => total load on region2 = 0.5*20 + 0.0*50 = 10
  //      - Static: region2 has tenant0 replica, so +5

  // final objective value (capacity limit is 45) = 0 + 0 + 0
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);

  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      regionTenantCount;
  for (auto& [traffic, region] : assignment) {
    auto tenant = replicaToTenant_.at(traffic);
    ++regionTenantCount[region][tenant];
  }

  // verify scopeItem utils w.r.t. "load" (including static values)
  verifyScopeItemAfterUtilMetrics(
      "load",
      "region",
      solution,
      {{"region0", 28.0}, // 25 + 3 static
       {"region1", 52.8}, // 44.8 + 5 + 3 static
       {"region2", 5.2}}, // 0.2 + 5 static
      {{"region0", 43.0}, // 35 + 5 + 3 static
       {"region1", 28.0}, // 25 + 3 static
       {"region2", 15.0}}); // 10 + 5 static

  // Verify group scope item util metrics (including static values)
  verifyScopeItemGroupUtilMetrics(
      "load",
      "region",
      solution,
      {
          // Initial metrics (scopeItem, partition, group, value)
          // These values include both dynamic and static components
          {"region0",
           "tenant",
           "tenant0",
           0.0}, // No tenant0 replica in region0
          {"region0", "tenant", "tenant1", 28.0}, // 25 (dynamic) + 3 (static)
          {"region1", "tenant", "tenant0", 24.8}, // 19.8 (dynamic) + 5 (static)
          {"region1", "tenant", "tenant1", 28.0}, // 25 (dynamic) + 3 (static)
          {"region2", "tenant", "tenant0", 5.2}, // 0.2 (dynamic) + 5 (static)
          {"region2",
           "tenant",
           "tenant1",
           0.0}, // No tenant1 replica in region2
      },
      {
          // Final metrics should be same as initial since no violations
          {"region0", "tenant", "tenant0", 15.0}, // 10 (dynamic) + 5 (static)
          {"region0", "tenant", "tenant1", 28.0}, // 25 (dynamic) + 3 (static)
          {"region1", "tenant", "tenant0", 0.0},
          {"region1", "tenant", "tenant1", 28.0}, // 25 (dynamic) + 3 (static)
          {"region2", "tenant", "tenant0", 15.0}, // 10 (dynamic) + 5 (static)
          {"region2", "tenant", "tenant1", 0.0},
      });

  // verify initial and final routingTrafficMetrics
  verifyTrafficMetrics(
      "routingConfig1",
      solution,
      {
          {"tenant0", "region0", "region0", 0.0},
          {"tenant0", "region0", "region1", 0.98},
          {"tenant0", "region0", "region2", 0.0},
          //
          {"tenant0", "region1", "region0", 0.0},
          {"tenant0", "region1", "region1", 0.01},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      }, // initial metrics (tenant, sourceRegion, destinationRegion, value)
      {
          {"tenant0", "region0", "region0", 0.49},
          {"tenant0", "region0", "region1", 0.0},
          {"tenant0", "region0", "region2", 0.49},
          //
          {"tenant0", "region1", "region0", 0.01},
          {"tenant0", "region1", "region1", 0.0},
          {"tenant0", "region1", "region2", 0.01},
          //
          {"tenant1", "region2", "region0", 0.5},
          {"tenant1", "region2", "region1", 0.5},
          {"tenant1", "region2", "region2", 0.0},
      } // final metrics (tenant, sourceRegion, destinationRegion, value)
  );
}

} // namespace facebook::rebalancer::interface::tests
