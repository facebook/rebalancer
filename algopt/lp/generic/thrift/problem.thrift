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

include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "algopt_lp_"}
@hack.LegacyOmitPrefixInNameString
package "meta.com/algopt/rebalancer"

namespace hack ""

namespace cpp2 facebook.algopt.lp.thrift
namespace py3 algopt.lp.thrift
namespace py algopt.lp.thrift.problem

enum ProblemStatus {
  OPTIMAL_FOUND = 0,
  SOLUTION_FOUND = 1,
  SOLUTION_NOT_FOUND = 2,
  NO_SOLUTION_EXISTS = 3,
}

enum WarmStartStatus {
  PROCESSING_ERROR = 0,
  FEASIBLE_SOLUTION_PROVIDED = 1,
  FEASIBLE_SOLUTION_OBTAINED = 2,
  FEASIBLE_SOLUTION_NOT_FOUND = 3,
  SOLUTION_IS_DROPPED = 4,
}

struct WarmStartResult {
  1: WarmStartStatus status;
  2: double processingTimeInSecs;
}

struct ProblemAttributes {
  1: i32 numVariables = -1;
  2: i32 numConstraints = -1;
  3: i32 numOfNonZeros = -1;
  // modelFingerprint is uint32_t
  4: optional string modelFingerprint;
  // Post-presolve dimensions (captured via solver callback or log parsing)
  5: optional i32 presolvedVariables;
  6: optional i32 presolvedConstraints;
  7: optional i32 presolvedNonZeros;
  // Matrix condition number.
  8: optional double matrixConditionNumber;
}

struct ProblemResult {
  1: ProblemStatus status;
  2: optional WarmStartResult warmStartResult;
  3: double absoluteGap = 1e10;
  4: double relativeGap = 1;
  5: double bestBound = 0;
  6: double bestObjective = 0;
  7: ProblemAttributes problemAttributes;
}
