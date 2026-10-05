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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <functional>
#include <optional>

namespace facebook::rebalancer {

using Profile =
    std::optional<std::reference_wrapper<interface::ProblemProfile>>;

class Solver {
 public:
  virtual ~Solver() = default;
  explicit Solver() = default;
  virtual bool solve(Problem& p, Profile profile = std::nullopt) = 0;
  virtual bool needs_continuous_expressions() {
    return false;
  }
};
} // namespace facebook::rebalancer
