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

#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/builders/RoutingConfigsBuilder.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/RoutingConfig.h"
#include "algopt/rebalancer/entities/RoutingRing.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <gtest/gtest.h>

#include <coroutine>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class MetricsTestBase : public ExpressionTestsBase {
 protected:
  MetricsTestBase() {
    universeBuilder_.setObjectTypeName("task");
    universeBuilder_.setContainerTypeName("host");
  }

  // to add empty routing config (for tests that just need the Id)
  folly::coro::Task<void> addEmptyRoutingConfig(
      const std::string& routingConfigName,
      const std::string& scopeName,
      const std::string& partitionName) {
    const auto scopeId = universeBuilder_.getScopeId(scopeName);
    const auto partitionId = universeBuilder_.getPartitionId(partitionName);
    const entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>
        emptyGroupToRoutingRings;
    co_await addRoutingConfig(
        routingConfigName,
        entities::RoutingConfigData{std::make_shared<entities::RoutingConfig>(
            emptyGroupToRoutingRings, nullptr, scopeId, partitionId, nullptr)});
  }
};

} // namespace facebook::rebalancer::packer::tests
