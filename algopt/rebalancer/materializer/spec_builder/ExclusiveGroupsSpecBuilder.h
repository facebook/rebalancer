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
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"

namespace facebook::rebalancer::materializer {

class ExclusiveGroupsSpecBuilder : public SpecBuilder {
 public:
  ExclusiveGroupsSpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::ExclusiveGroupsSpec spec,
      std::shared_ptr<LogCollector> logger);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  // NOTE: scopeItem to group mapping is built when constraint() or goal() is
  // called, so tagging information is only shown once the mapping is built.
  std::string description() const override;

  SpecParameters getSpecInfo() const override;

  // We pre-assign scope items to groups based on 3 metrics (deficit, moves,
  // deviation). This function computes and returns that mapping
  folly::coro::Task<PackerMap<std::string, std::string>>
  computeScopeItemToGroupAssignment(ExpressionBuilder& expressionBuilder) const;

 private:
  interface::ExclusiveGroupsSpec spec_;
  std::shared_ptr<LogCollector> logger_;
  // this depends on mapping of scope items to groups and is set after we call
  // constraint(...)
  mutable std::string taggingDescription_ = "unset";
};

} // namespace facebook::rebalancer::materializer
