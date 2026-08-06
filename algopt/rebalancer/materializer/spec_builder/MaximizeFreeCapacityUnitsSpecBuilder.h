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

#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"

namespace facebook::rebalancer::materializer {

class MaximizeFreeCapacityUnitsSpecBuilder : public SpecBuilder {
 public:
  MaximizeFreeCapacityUnitsSpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::MaximizeFreeCapacityUnitsSpec spec,
      bool continuousExpressions);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  [[noreturn]] folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

 private:
  interface::MaximizeFreeCapacityUnitsSpec spec_;
  bool continuousExpressions_;
  entities::DimensionId dimensionId_;
};

} // namespace facebook::rebalancer::materializer
