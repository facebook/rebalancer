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

#include "algopt/rebalancer/examples/common/BundleOutput.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <folly/logging/xlog.h>
#include <folly/Random.h>
#include <folly/String.h>

#include <array>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace facebook::rebalancer::interface;

static const std::array<std::string, 4> regions = {"prn", "frc", "lla", "ftw"};
static const std::map<std::string, double> max_rps = {
    {"prn", 10000},
    {"frc", 10000},
    {"lla", 5000},
    {"ftw", 10000},
};
static constexpr double max_utilization = 0.8;

static const std::array<std::string, 5> continents =
    {"europe", "asia", "africa", "north_america", "south_america"};
static const std::map<std::string, std::map<std::string, double>> latencies = {
    {"europe", {{"prn", 4.0}, {"lla", 0.2}, {"frc", 3.0}, {"ftw", 3.1}}}};
static constexpr double default_latency = 1.0;
static constexpr int buckets_per_continent = 100;
static constexpr int max_rps_per_bucket = 100;

static void set_random_initial_state(ProblemSolver& solver) {
  std::map<std::string, std::vector<std::string>> initial_assignment;
  for (const auto& continent : continents) {
    for (const auto bucket : folly::irange(buckets_per_continent)) {
      auto random_region = folly::Random::rand32(regions.size());
      initial_assignment[regions[random_region]].emplace_back(
          fmt::format("bucket_{}_{}", continent, bucket));
    }
  }
  solver.setAssignment(initial_assignment);
}

static void populate_with_random_rps(std::map<std::string, double>& rps) {
  for (const auto& continent : continents) {
    for (const auto bucket : folly::irange(buckets_per_continent)) {
      rps[fmt::format("bucket_{}_{}", continent, bucket)] =
          folly::Random::rand32(max_rps_per_bucket);
    }
  }
}

// add affinities from buckets to regions based on its relation to avg latency
static void reduce_latency_goal(
    ProblemSolver& solver,
    const std::map<std::string, double>& rps,
    double weight) {
  std::vector<AssignmentAffinity> affinity_specs;
  auto get_latency_with_default = [](const std::string& continent,
                                     const std::string& region) -> double {
    const auto region_latencies_it = latencies.find(continent);
    if (region_latencies_it == latencies.end()) {
      return default_latency;
    }
    const auto region_latency_it = region_latencies_it->second.find(region);
    if (region_latency_it == region_latencies_it->second.end()) {
      return default_latency;
    }
    return region_latency_it->second;
  };

  double max_latency = 0.0;
  for (const auto& region : regions) {
    for (const auto& continent : continents) {
      for (const auto bucket : folly::irange(buckets_per_continent)) {
        const auto& bucket_name =
            fmt::format("bucket_{}_{}", continent, bucket);
        const double latency = get_latency_with_default(continent, region);
        // Negative value in afinity in order to make this bucket have less
        // affinity for cases where latency is higher.
        // Scale by RPS since we are targeting the average latency and
        // buckets with higher RPS would contribute more to the average.
        AssignmentAffinity affinity;
        affinity.objectName() = bucket_name;
        affinity.scopeItemName() = region;
        affinity.affinity() = -rps.at(bucket_name) * latency;

        affinity_specs.emplace_back(affinity);
        max_latency = max_latency > latency ? max_latency : latency;
      }
    }
  }

  // Normalize affinities
  // Rebalancer will use a sum of affinities of all assignments as a measure
  // objective value for any goal. If we don't do this scaling then latency
  // goal will dominate, it will have a higher absolute value compared to
  // other goals. With scaling down, we give other goals better odds of
  // competing with this one.
  int available_rps = 0;
  for (const auto& [_, val] : rps) {
    available_rps += val;
  }
  double normalize_by = available_rps * max_latency;
  normalize_by = normalize_by > 0 ? normalize_by : 1.0;
  for (auto& affinity_spec : affinity_specs) {
    *affinity_spec.affinity() /= normalize_by;
  }

  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.affinities() = affinity_specs;

  solver.addGoal(assignmentAffinitiesSpec, weight);
}

static void analyze_result(
    const AssignmentSolution& solution,
    const std::map<std::string, double>& rps) {
  std::map<std::string, double> region_rps;
  std::map<std::pair<std::string, std::string>, double> continent_region_rps;

  std::vector<std::string_view> bucket_parts;
  for (const auto& [bucket, region] : *solution.assignment()) {
    bucket_parts.clear();
    folly::split('_', bucket, bucket_parts);
    if (bucket_parts.empty()) {
      XLOG(FATAL) << fmt::format(
          "{} must look like bucket_{{continent}}_{{region_id}}", bucket);
    }
    region_rps[region] += rps.at(bucket);
    continent_region_rps[{std::string(bucket_parts.at(1)), region}] +=
        rps.at(bucket);
  }

  std::stringstream ss;
  ss << "\nHow are we doing on utilization balancing goal?" << std::endl;
  for (const auto& region : regions) {
    fmt::format_to(
        std::ostream_iterator<char>(ss),
        "Utilization of {} is {:.2f}%\n",
        region,
        100 * region_rps[region] / max_rps.at(region));
  }
  ss << "\nHow are we doing on latency goal?" << std::endl;
  ss << "europe :\n";

  for (const auto& region : regions) {
    auto region_rps_it = continent_region_rps.find({"europe", region});
    if (region_rps_it == continent_region_rps.end()) {
      continue;
    }
    fmt::format_to(
        std::ostream_iterator<char>(ss),
        "\t{}:\t{}\n",
        region,
        region_rps_it->second);
  }
  ss << "Ideally, traffic from europe should prefer to go to lla." << std::endl;
  XLOG(INFO) << ss.str();
}

static void balance_web_tiers() {
  auto solver = ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");
  solver->setObjectName("bucket");
  solver->setContainerName("region");

  // set initial assignment
  set_random_initial_state(*solver);

  // populate with rps (dimension) per bucket (object) data
  std::map<std::string, double> rps;
  populate_with_random_rps(rps);
  solver->addObjectDimension("rps", rps);
  solver->addContainerDimension("rps", max_rps);

  // add capacity constraint for rps with in a region
  auto spec = CapacitySpec();
  spec.scope() = "region";
  spec.dimension() = "rps";
  spec.limit()->globalLimit() = max_utilization;
  solver->addConstraint(std::move(spec));

  // add goals to optimize for
  // balance has a lower weight by design, in order to max-out
  // the traffic to LLA coming from Europe (lowest latency).
  reduce_latency_goal(*solver, rps, 100.0);
  // minimize movement of buckets
  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "region";
  minimizeMovementSpec.dimension() = "rps";
  solver->addGoal(std::move(minimizeMovementSpec), 100.0);

  // balance utilization across regions
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "region";
  balanceSpec.dimension() = "rps";
  solver->addGoal(std::move(balanceSpec), 50.0);

  OptimalSolverSpec solver_spec;
  solver_spec.solverPackage() = OptimalSolverPackage::HIGHS;
  solver->addSolver(solver_spec);

  const auto& solution = solver->solve();
  facebook::rebalancer::examples::maybeSaveBundle(*solver);
  analyze_result(solution, rps);
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  balance_web_tiers();

  return 0;
}
