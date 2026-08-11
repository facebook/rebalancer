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

#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"

#include <memory>

namespace facebook::rebalancer::materializer {

// Makes the appropriate SpecBuilder given a constraint or goal spec.
class SpecBuilderFactory {
 public:
  SpecBuilderFactory(
      std::shared_ptr<const entities::Universe> universe,
      bool continuousExpressions,
      std::shared_ptr<LogCollector> logger);

  std::unique_ptr<SpecBuilder> getBuilder(
      const interface::ConstraintSpecs& spec) const;
  std::unique_ptr<SpecBuilder> getBuilder(
      const interface::GoalSpecs& spec) const;

 private:
  std::shared_ptr<const entities::Universe> universe_;
  bool continuousExpressions_;
  std::shared_ptr<LogCollector> logger_;
};

} // namespace facebook::rebalancer::materializer
