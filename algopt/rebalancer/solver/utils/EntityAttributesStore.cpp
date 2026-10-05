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

#include "algopt/rebalancer/solver/utils/EntityAttributesStore.h"

#include "algopt/rebalancer/solver/utils/EntityAttributes.h"

#include <folly/container/MapUtil.h>

namespace facebook::rebalancer {

void EntityAttributesStore::registerAttributes(
    const std::string& key,
    std::shared_ptr<EntityAttributes> attributes) {
  if (folly::get_ptr(nameToAttributes_, key)) {
    throw std::runtime_error(
        fmt::format("EntityAttributes with key {} already exists", key));
  }
  nameToAttributes_.emplace(key, std::move(attributes));
}

void EntityAttributesStore::updateOnApply(const ChangeSet& changes) {
  for (auto& [_, entityAttr] : nameToAttributes_) {
    entityAttr->updateOnApply(changes);
  }
}

} // namespace facebook::rebalancer
