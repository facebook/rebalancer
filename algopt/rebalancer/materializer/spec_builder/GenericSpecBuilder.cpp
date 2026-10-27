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

#include "algopt/rebalancer/materializer/spec_builder/GenericSpecBuilder.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/materializer/spec_builder/CapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupCountSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/LogicalAndSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/LogicalOrSpecBuilder.h"

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {

GenericSpecBuilder::GenericSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::GenericSpec spec)
    : SpecBuilder(universe) {
  switch (spec.getType()) {
    case interface::GenericSpec::Type::logicalOrSpec:
      builder_ = std::make_unique<LogicalOrSpecBuilder>(
          universe, spec.logicalOrSpec().as_const().value());
      break;
    case interface::GenericSpec::Type::logicalAndSpec:
      builder_ = std::make_unique<LogicalAndSpecBuilder>(
          universe, spec.logicalAndSpec().as_const().value());
      break;
    case interface::GenericSpec::Type::capacitySpec:
      builder_ = std::make_unique<CapacitySpecBuilder>(
          universe, spec.capacitySpec().as_const().value());
      break;
    case interface::GenericSpec::Type::groupCountSpec:
      builder_ = std::make_unique<GroupCountSpecBuilder>(
          universe, spec.groupCountSpec().as_const().value());
      break;
    case interface::GenericSpec::Type::groupCapacitySpec:
      builder_ = std::make_unique<GroupCapacitySpecBuilder>(
          universe, spec.groupCapacitySpec().as_const().value());
      break;
    case interface::GenericSpec::Type::__EMPTY__:
      throw std::runtime_error("Uninitialized generic spec");
  }
}

folly::coro::Task<std::vector<ConstraintInfo>> GenericSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  return builder_->constraints(expressionBuilder);
}

folly::coro::Task<ExprPtr> GenericSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  return builder_->goalCoro(expressionBuilder);
}

std::string GenericSpecBuilder::description() const {
  return builder_->description();
}

SpecParameters GenericSpecBuilder::getSpecInfo() const {
  return builder_->getSpecInfo();
}

} // namespace facebook::rebalancer::materializer
