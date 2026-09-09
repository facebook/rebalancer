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

#include "algopt/rebalancer/entities/thrift/gen-cpp2/Entities_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"

namespace entities = facebook::rebalancer::entities;
namespace interface = facebook::rebalancer::interface;

class BackwardCompatabilityUtils {
 public:
  static void possiblyModify(interface::AssignmentProblem& problem);

 private:
  static void possiblyModify(entities::thrift::Universe& universe);
  static void possiblyModify(entities::thrift::Goals& goals);
  static void possiblyModify(entities::thrift::Constraints& constraints);
  static void possiblyModify(interface::GoalSpecs& goalSpec);
  static void possiblyModify(interface::ConstraintSpecs& constraintSpec);

  static void possiblyModify(interface::RoutingLatencySpec& spec);
  static void possiblyModify(interface::ExclusiveScopeItemsSpec& spec);
  static void possiblyModify(interface::MinimizeContainersSpec& spec);
};
