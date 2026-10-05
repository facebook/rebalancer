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

#include "algopt/rebalancer/solver/solvers/SolverFactory.h"

#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/solvers/ChainSolver.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchSolver.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchStageSolver.h"
#include "algopt/rebalancer/solver/solvers/OptimalSolver.h"
#include "algopt/rebalancer/solver/solvers/OptimalSubsetSolver.h"
#include "algopt/rebalancer/solver/solvers/Solver.h"

#include <thrift/lib/cpp2/FieldRef.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/solver/solvers/fb/RasHybridSolver.h"
#endif

using namespace std;

namespace facebook::rebalancer {

std::unique_ptr<Solver> SolverFactory::createSolver(const SolverT& solver) {
  switch (solver.getType()) {
    case SolverT::Type::localSearchSolverSpec: {
      return make_unique<LocalSearchSolver>(*solver.localSearchSolverSpec());
    }
    case SolverT::Type::localSearchStageSolverSpec:
      return make_unique<LocalSearchStageSolver>(
          *solver.localSearchStageSolverSpec());
    case SolverT::Type::optimalSolverSpec:
      return make_unique<OptimalSolver>(*solver.optimalSolverSpec());
    case SolverT::Type::optimalSubsetSolverSpec:
      return make_unique<OptimalSubsetSolver>(
          *solver.optimalSubsetSolverSpec());
#ifndef REBALANCER_OSS_BUILD
    case SolverT::Type::rasHybridSolverSpec:
      return make_unique<RasHybridSolver>(*solver.rasHybridSolverSpec());
#endif
    default:
      throw std::runtime_error("Unknown solver type");
  }
}

unique_ptr<Solver> SolverFactory::createSolver(const StrategyT& strategy) {
  vector<unique_ptr<Solver>> solvers;
  solvers.reserve(strategy.solvers()->size());
  std::transform(
      strategy.solvers()->begin(),
      strategy.solvers()->end(),
      std::back_inserter(solvers),
      [](const auto& solver) { return createSolver(solver); });
  return make_unique<ChainSolver>(std::move(solvers));
}

} // namespace facebook::rebalancer
