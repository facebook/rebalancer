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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/small_vector.h>

#include <memory>
#include <string_view>

namespace facebook::rebalancer {

struct MinPresenceConfig {
  materializer::LimitWrapper groupToPresenceWeight;
  materializer::LimitWrapper groupToExtraAdditivePenalty;
  folly::F14FastMap<
      interface::GroupUtilMultiplierTarget,
      folly::small_vector<materializer::LimitWrapper, 2>>
      groupUtilMultiplierMap;
  folly::F14FastMap<entities::ScopeItemId, folly::F14FastSet<entities::GroupId>>
      scopeItemToAlwaysPresentGroups;
};

// Policy for ObjectPartitionLookup that uses group min presence
struct ObjectPartitionLookupWithMinPresencePolicy {
  static constexpr std::string_view typeName =
      "ObjectPartitionLookupWithMinPresence";

  struct Data {
    Data(
        std::shared_ptr<const MinPresenceConfig> config,
        bool makeContinuousPenaltyTerm,
        bool roundUpGroupUtilOnScopeItem)
        : config_(std::move(config)),
          makeContinuousPenaltyTerm(makeContinuousPenaltyTerm),
          roundUpGroupUtilOnScopeItem(roundUpGroupUtilOnScopeItem) {}

    std::shared_ptr<const MinPresenceConfig> config_;
    bool makeContinuousPenaltyTerm = false;
    bool roundUpGroupUtilOnScopeItem = false;

    double applyWeights(
        double value,
        const entities::GroupId& groupId,
        const entities::ScopeItemId& scopeItemId,
        std::initializer_list<interface::GroupUtilMultiplierTarget> targets,
        const Precision& precision,
        bool applyCeil = false) const;
  };
};

} // namespace facebook::rebalancer
