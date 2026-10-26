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

#pragma once

#include "algopt/rebalancer/solver/solvers/Solver.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <memory>
#include <vector>

namespace facebook::rebalancer {

class ChainSolver : public Solver {
 public:
  explicit ChainSolver(std::vector<std::unique_ptr<Solver>> solvers);
  bool solve(Problem& p, Profile profile = std::nullopt) override;
  bool needs_continuous_expressions() override;
  bool needsInvalidMoveFilter() const override;

 private:
  std::vector<std::unique_ptr<Solver>> solvers_;
};
} // namespace facebook::rebalancer
