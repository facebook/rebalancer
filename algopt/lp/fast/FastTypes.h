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

#include "algopt/lp/generic/Relation.h"
#include "algopt/lp/generic/Variable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace facebook::algopt::lp {

struct Term {
  double coefficient;
  uint64_t variableId;
};

struct InnerVariable {
  std::string name;
  VariableImpl::Type type;
  double lb;
  double ub;
  std::optional<double> threshold;
  std::optional<double> initialValue;
  mutable std::optional<double> value;
};

struct InnerConstraint {
  std::vector<Term> terms;
  double rhs;
  Relation::ConstantType sense;
  std::string name;
  bool deleted = false;
};

} // namespace facebook::algopt::lp
