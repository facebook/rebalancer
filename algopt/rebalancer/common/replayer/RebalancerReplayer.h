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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"

#include <string>

namespace facebook {
namespace rebalancer {

class RebalancerReplayer {
 public:
  // Returns the filename of a local copy of the problem.
  static interface::AssignmentProblem downloadFromManifold(
      const std::string& runId);

  // Replays a problem given its local filename.
  static facebook::rebalancer::interface::AssignmentSolution replay(
      interface::AssignmentProblem&& problem,
      std::optional<std::string> loggingLabel = std::nullopt);

  // A persisted instance carries RasHybridSolverSpec.solverLogFile pointing at
  // a temp path which does not exist on a replay/benchmark host. Clearing it to
  // avoid failures from writing logs.
  static void clearSolverLogFile(interface::AssignmentProblem& problem);
};

} // namespace rebalancer
} // namespace facebook
