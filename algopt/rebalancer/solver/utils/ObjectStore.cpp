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

#include "algopt/rebalancer/solver/utils/ObjectStore.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectValueTypes.h"

#include <stdint.h>

// MurmurHash64A performance-optimized for hash of uint64_t keys
uint64_t MurmurRehash64A(uint64_t key) {
  constexpr uint64_t kMurmur2Seed = 4193360111ul;
  constexpr uint64_t m = 0xc6a4a7935bd1e995;
  constexpr int r = 47;

  uint64_t h = (uint64_t)kMurmur2Seed ^ (sizeof(uint64_t) * m);

  key *= m;
  key ^= key >> r;
  key *= m;

  h ^= key;
  h *= m;

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

namespace facebook::rebalancer {

ObjectScores::ObjectScores(entities::ObjectValues objectValues)
    : objectValues_(
          std::make_shared<const entities::ObjectValues>(
              std::move(objectValues))) {}

bool ObjectScores::operator()(entities::ObjectId lhs, entities::ObjectId rhs)
    const {
  const auto lhsValue = objectValues_->getObjectValue(lhs);
  const auto rhsValue = objectValues_->getObjectValue(rhs);

  if (lhsValue == rhsValue) {
    return MurmurRehash64A(static_cast<size_t>(lhs)) <
        MurmurRehash64A(static_cast<size_t>(rhs));
  }

  return lhsValue > rhsValue;
}

ObjectStore::Factory::Factory(
    std::shared_ptr<const ObjectScores> scores,
    bool useDynamicObjectOrdering)
    : scores_(std::move(scores)),
      useDynamicObjectOrdering_(useDynamicObjectOrdering) {}

ObjectStore ObjectStore::Factory::get() const {
  if (scores_ == nullptr) {
    return ObjectStore(useDynamicObjectOrdering_);
  }

  return ObjectStore(scores_.get(), useDynamicObjectOrdering_);
}

void ObjectStore::insert(entities::ObjectId object) {
  return std::visit(
      [object](auto&& container) { container.insert(object); }, objects_);
}

void ObjectStore::erase(entities::ObjectId object) {
  return std::visit(
      [object](auto&& container) { container.erase(object); }, objects_);
}

size_t ObjectStore::size() const {
  return std::visit(
      [](auto&& container) -> size_t { return container.size(); }, objects_);
}

bool ObjectStore::empty() const {
  return std::visit(
      [](auto&& container) { return container.empty(); }, objects_);
}

bool ObjectStore::contains(entities::ObjectId object) const {
  return std::visit(
      [object](auto&& container) { return container.contains(object); },
      objects_);
}

ObjectStore::Iterator ObjectStore::begin() const {
  return std::visit(
      [](auto&& container) { return ObjectStore::Iterator(container.begin()); },
      objects_);
}

ObjectStore::Iterator ObjectStore::end() const {
  return std::visit(
      [](auto&& container) { return ObjectStore::Iterator(container.end()); },
      objects_);
}

} // namespace facebook::rebalancer
