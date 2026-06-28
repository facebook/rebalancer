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

package "meta.com/algopt/rebalancer"

include "algopt/rebalancer/solver/if/packer.thrift"
include "configerator/structs/thrift_explorer/annotations.thrift" as thrift_explorer
include "algopt/rebalancer/interface/thrift/Types.thrift"
include "algopt/rebalancer/entities/thrift/Entities.thrift"
include "thrift/annotation/thrift.thrift"

namespace cpp2 facebook.rebalancer.interface
namespace php rebalancer_interface
namespace py3 rebalancer.interface.thrift

@thrift.ReserveIds{ids = [4, 9, 11, 18]}
struct AssignmentProblem {
  1: string jsonSpecFilepath;
  2: packer.StrategyT strategy;
  3: bool solutionSummaryEnabled;
  5: bool profilerEnabled;
  6: string service_;
  // following defines "scope" of the service and used for logging
  // not to be confused with container scopes
  7: string scope;
  8: Types.ConstraintPolicy constraintPolicy;
  10: list<string> summaryOptions;
  12: optional Types.MoveValidatorSpec moveValidator;
  13: bool enableScubaLogger = true;
  14: Types.MoveStatsSpec moveStatsSpec;
  15: string runId = "";
  16: Types.ConstraintParams defaultConstraintParams;
  17: list<string> descendingHotnessContainersOverride;
  // user can specify a special scope (which is a partition over containers)
  // this special scope is used by LP-based solvers to decompose into subproblems
  19: optional string decompositionScopeName;
  20: optional Entities.Universe universe;
  // publish metrics like utilization of a container (and other values of interest) to Assignment.initialMetrics() and AssignmentSolution.finalMetrics()
  21: bool publishMetrics = false;
  22: bool publishEquivalenceSetsInfo = false;
  23: bool useDynamicObjectOrdering = false;
  // When enabled, we compute disallowed (Object, Container) pairs and skip them
  // upfront. This improves latency when constraint violations invalidate many
  // evaluations, but consumes extra memory proportional to the number of
  // invalid (object, container) pairs.
  24: bool enableInvalidMoveFilter = false;

  // this field is used internally to add specific labels to the rebalancer_run_info scuba table; mainly used when running experiments to test changes
  1024: optional string scubaLoggingLabel;
}

@thrift_explorer.Context{
  description = "A bundle of a rebalancer problem and the corresponding solution if any.",
}
struct Bundle {
  1: AssignmentProblem problem;
  2: optional Types.AssignmentSolution solution;
}
