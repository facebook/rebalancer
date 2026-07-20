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

#include "algopt/rebalancer/solver/moves/MoveType.h"

#include <memory>

namespace facebook::rebalancer {

class MoveTypeFactory {
 public:
  static std::vector<std::shared_ptr<MoveType>> createMoveTypes(
      const interface::LocalSearchSolverSpec& spec,
      const Problem& problem);

  // This function is only used in stand alone solver to replay saved scenarios
  // that use moveTypes() list or moveTypeName for MoveTypeSpec.
  static void transformMoveTypesForReplayingSavedInstances(
      interface::LocalSearchSolverSpec& spec);

 private:
  static std::unique_ptr<MoveType> createMoveType(
      const std::string& name,
      const interface::LocalSearchSolverSpec& configs =
          interface::LocalSearchSolverSpec());
  static std::unique_ptr<MoveType> createMoveType(
      const interface::MoveTypeSpec& spec,
      const interface::LocalSearchSolverSpec& configs,
      const Problem& problem);
  static void convertMoveTypesToMoveTypeSpecs(
      interface::LocalSearchSolverSpec& spec);
  static void transformMoveTypeSpecs(interface::LocalSearchSolverSpec& spec);
};

} // namespace facebook::rebalancer
