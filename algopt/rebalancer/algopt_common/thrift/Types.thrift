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

@hack.NamePrefix{prefix = "facebook_algopt_common_thrift_"}
@hack.LegacyOmitPrefixInNameString
@hack.ConstantsClass{name = "facebook_algopt_common_thrift_CONSTANTS"}
package "meta.com/algopt/rebalancer"

namespace hack ""

namespace cpp2 facebook.algopt.common.thrift
namespace py3 facebook.algopt.common.thrift
namespace py facebook.algopt.common.thrift.Types

enum Intent {
  MAX = 0,
}

@hack.MigrationBlockingAllowInheritance
struct AllowedWorsening {
  1: double percent = 0.0;
  2: double absolute = 0.0;
  3: Intent intent = Intent.MAX;
}

@hack.MigrationBlockingAllowInheritance
struct HigherPriorityObjectivesConfig {
  // for each tuple position i, if obj_i is the objective in that position and if a value `d_i` is set for i, then it allows the
  // solver to worsen the objective to at most
  // 1) max((1 + d_i.percent/100) * v_i, v_i + d_i.absolute), when d.intent is MAX
  // where v_i is the value of obj_i at the start of the stage in the case of LocalSearchStageSolver
  // and is equal to best objective value found during the solve for minimizing ob_i when using the optimalSolver
  // if no value is set for tuple position i, the objective will never be worsened
  1: map<i32, AllowedWorsening> tuplePosToAllowedWorsening;
}

// Thresholds on a numeric value.
struct Threshold {
  1: optional double percent;
  2: optional double absolute;
}

enum FilterType {
  ALLOWLIST = 0,
  BLOCKLIST = 1,
}

// helper struct to select a subset of string using allowlist or blocklist
struct StringListFilterConfig {
  // list of string to filter in (allowlist) or filter out (blocklist)
  1: list<string> stringsToFilter;
  2: FilterType filterType = FilterType.ALLOWLIST;
}

// Storing per objective parameters by index with a default
struct PerObjectiveValue {
  1: map<i64, double> objectiveIndexToValue;
  2: double defaultValue;
}

const double DEFAULT_PRECISION_TOLERANCE = 1e-10;
struct PrecisionTolerances {
  1: double absolute = DEFAULT_PRECISION_TOLERANCE;
  2: double relative = DEFAULT_PRECISION_TOLERANCE;
}
