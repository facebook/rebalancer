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

#include <folly/init/Init.h>
#include <folly/logging/xlog.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace facebook::rebalancer::interface;

DEFINE_bool(
    objective_tuple,
    false,
    ("Will use an objective tuple to capture cpu and network balance goals "
     "instead of a weighted linear sum"));

DEFINE_bool(
    local_search,
    false,
    "Will use local search solver instead of optimal solver if set");

DEFINE_int32(max_solve_time, 600, "Max solve time in seconds");

// cpu requirements of shards (cpu object dimension)
static const std::map<std::string, double> shard_cpu = {
    {"s1", 40},
    {"s2", 40},
    {"s3", 40},
    {"s4", 20},
    {"s5", 20},
};

// cpu capacity of hosts (cpu container dimension)
static const std::map<std::string, double> host_cpu = {
    {"h1", 100},
    {"h2", 100},
    {"h3", 100},
    {"h4", 100},
};

// grouping of hosts into rack scope
static const std::map<std::string, std::vector<std::string>> rack_to_hosts = {
    {"r1", {"h1", "h2"}},
    {"r2", {"h3", "h4"}}};

// network requirement of shards (network object dimension)
static const std::map<std::string, double> shard_network = {
    {"s1", 400},
    {"s2", 400},
    {"s3", 200},
    {"s4", 400},
    {"s5", 400},
};

// network capacity of racks (newtwork rack scope dimension)
std::map<std::string, double> rack_network = {
    {"r1", 1000},
    {"r2", 1000},
};

static void set_initial_assignment(ProblemSolver& solver) {
  // extract hosts from shard_cpu reqirements spec
  std::vector<std::string> shards;
  shards.reserve(shard_cpu.size());
  for (const auto& [shard, _] : shard_cpu) {
    shards.emplace_back(shard);
  }

  std::map<std::string, std::vector<std::string>> initial_assignments;
  // allocate all shards to a 'dummy' host
  initial_assignments["dummy"] = shards;

  // extract hosts from host_cpu capacity spec
  for (const auto& [host, _] : host_cpu) {
    initial_assignments[host] = {};
  }

  solver.setAssignment(initial_assignments);
}

static void populate_host_to_rack_mapping(
    std::map<std::string, std::string>& host_to_rack) {
  for (const auto& [rack, hosts] : rack_to_hosts) {
    for (const auto& host : hosts) {
      host_to_rack[host] = rack;
    }
  }
}

static void print_solution(
    const AssignmentSolution& solution,
    const std::map<std::string, std::string>& host_to_rack) {
  std::stringstream ss;
  for (const auto& [shard, host] : *solution.assignment()) {
    fmt::format_to(
        std::ostream_iterator<char>(ss),
        " {} : {}, {}\n",
        shard,
        host,
        host_to_rack.at(host));
  }
  XLOG(INFO) << "Final assignment: \n" << ss.str() << std::endl;

  // accumulate host cpu and rack network utilization
  std::map<std::string, int> host_cpu_assignment;
  std::map<std::string, int> rack_network_assignment;
  for (const auto& [shard, host] : *solution.assignment()) {
    host_cpu_assignment[host] += shard_cpu.at(shard);
    rack_network_assignment[host_to_rack.at(host)] += shard_network.at(shard);
  }

  // print host cpu utilization
  ss.clear();
  ss.str("");
  for (const auto& [host, cpu] : host_cpu_assignment) {
    fmt::format_to(
        std::ostream_iterator<char>(ss),
        "{}: {:.2f}\n",
        host,
        cpu * 1.0 / host_cpu.at(host));
  }
  XLOG(INFO) << "Host cpu utilization:\n" << ss.str() << std::endl;

  // print rack network utilization
  ss.clear();
  ss.str("");
  for (const auto& [rack, network] : rack_network_assignment) {
    fmt::format_to(
        std::ostream_iterator<char>(ss),
        "{}: {:.2f}\n",
        rack,
        network * 1.0 / rack_network.at(rack));
  }
  XLOG(INFO) << "Rack network utilization:\n" << ss.str() << std::endl;
}

// code is based on fbcode/algopt/rebalancer/interface/Example.cpp
int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  // useLocalFiles = true => json files are created on disk
  // allowing for further debugging/learning.
  // it also allows us to re-run the problem, with potentially using a
  // different solver/strategy
  //
  // Outputs a log line like underneath:
  // To re-run the solver, execute: buck run
  // algopt/rebalancer/solver/solver:packer --
  // --materializer --input_file /tmp/uEul48/config.json --output_file
  // /tmp/uEul48/output.thrift --move_list SINGLE,SWAP,TRIPLE_LOOP,KL_SEARCH
  auto solver =
      ProblemSolverFactory::makeProblemSolver("rebalancer", "examples", true);

  // set object and container names
  solver->setObjectName("shard");
  solver->setContainerName("host");

  // set the initial assignment
  set_initial_assignment(*solver);

  // set cpu dimension
  solver->addObjectDimension("cpu", shard_cpu);
  solver->addContainerDimension("cpu", host_cpu);

  // define rack scope
  std::map<std::string, std::string> host_to_rack;
  populate_host_to_rack_mapping(host_to_rack);
  solver->addScope("rack", host_to_rack);

  // set network dimension
  solver->addObjectDimension("network", shard_network);
  solver->addScopeDimension("network", "rack", rack_network);

  // remove all shards from 'dummy' host
  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"dummy"};
  solver->addConstraint(toFreeSpec);

  // add capacity constraints
  CapacitySpec hostCapacitySpec;
  hostCapacitySpec.scope() = "host";
  hostCapacitySpec.dimension() = "cpu";
  solver->addConstraint(hostCapacitySpec);

  CapacitySpec rackCapacitySpec;
  rackCapacitySpec.scope() = "rack";
  rackCapacitySpec.dimension() = "network";
  solver->addConstraint(rackCapacitySpec);

  if (FLAGS_objective_tuple) {
    // express cpu balance and network balance as two objectives in the
    // objective tuple.
    // Doing so ensures that we strictly prefer solutions that improve the first
    // objective over all others, given first objective already attained its
    // best value, then it prefers solutions that stricly improve second
    // objective over all others and so on. Think of comparing two tuples of
    // equal size (a, b) < (a1, b1) iff a < a1 || a == a1 && b < b1 etc.

    // balancing cpu is top priority
    BalanceSpec hostBalanceSpec;
    hostBalanceSpec.scope() = "host";
    hostBalanceSpec.dimension() = "cpu";
    solver->addGoal(hostBalanceSpec);
    solver->addGoalBoundary();

    // as a secondary goal also balance network
    BalanceSpec rackBalanceSpec;
    rackBalanceSpec.scope() = "rack";
    rackBalanceSpec.dimension() = "network";
    solver->addGoal(rackBalanceSpec);
  } else {
    // express both goals as a single objective via weighted sum
    BalanceSpec hostBalanceSpec;
    hostBalanceSpec.scope() = "host";
    hostBalanceSpec.dimension() = "cpu";
    solver->addGoal(hostBalanceSpec, 100);

    BalanceSpec rackBalanceSpec;
    rackBalanceSpec.scope() = "rack";
    rackBalanceSpec.dimension() = "network";
    solver->addGoal(rackBalanceSpec, 10);
  }

  solver->enableMoveStats();

  if (FLAGS_local_search) {
    LocalSearchSolverSpec spec;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
    spec.solveTime() = FLAGS_max_solve_time;
    solver->addSolver(spec);
  } else {
    OptimalSolverSpec spec;
    spec.solverPackage() = OptimalSolverPackage::HIGHS;
    if (FLAGS_max_solve_time == 0) {
      spec.skipMipSolveForTesting() = true;
    } else {
      spec.solveTime() = FLAGS_max_solve_time;
    }
    solver->addSolver(spec);
  }

  const auto& solution = solver->solve();
  facebook::rebalancer::examples::maybeSaveBundle(*solver);
  print_solution(solution, host_to_rack);

  return 0;
}
