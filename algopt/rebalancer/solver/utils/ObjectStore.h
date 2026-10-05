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

#include "algopt/rebalancer/algopt_common/AccessOrderedFastSet.h"
#include "algopt/rebalancer/algopt_common/AccessOrderedSortedSet.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectValueTypes.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <boost/iterator/iterator_facade.hpp>
#include <folly/container/MapUtil.h>
#include <folly/lang/Hint.h>

#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <stddef.h>
#include <type_traits>
#include <utility>
#include <variant>

namespace facebook::rebalancer {

class ObjectScores {
 public:
  ObjectScores() = default;
  explicit ObjectScores(entities::ObjectValues objectValues);

  bool operator()(entities::ObjectId lhs, entities::ObjectId rhs) const;

 private:
  // `std::set` and the dynamic sorted container copy comparators. Keep the
  // value storage behind a shared pointer so those copies stay cheap and all
  // comparators observe the same immutable backing data.
  std::shared_ptr<const entities::ObjectValues> objectValues_ = nullptr;
};

class ObjectStore {
  using SortedObjects =
      std::set<entities::ObjectId, ObjectScores /*comparator*/>;
  using UnorderedObjects = PackerSet<entities::ObjectId>;

  using DynamicUnorderedObjects =
      algopt::AccessOrderedFastSet<entities::ObjectId>;
  using DynamicSortedObjects =
      algopt::AccessOrderedSortedSet<entities::ObjectId, ObjectScores>;

 public:
  class Factory;
  class Iterator;

 public:
  void insert(entities::ObjectId object);
  void erase(entities::ObjectId object);

  // const methods
  size_t size() const;
  bool empty() const;
  bool contains(entities::ObjectId object) const;

  // iterator methods
  Iterator begin() const;
  Iterator end() const;

 private:
  // when object scores are provided, the objects will be ordered by scores.
  explicit ObjectStore(
      const ObjectScores* scores,
      bool useDynamicObjectOrdering) {
    if (useDynamicObjectOrdering) {
      objects_.emplace<DynamicSortedObjects>(/*comparator=*/*scores);
    } else {
      objects_.emplace<SortedObjects>(/*comparator=*/*scores);
    }
  }

  explicit ObjectStore(bool useDynamicObjectOrdering) {
    if (useDynamicObjectOrdering) {
      objects_.emplace<DynamicUnorderedObjects>();
    } else {
      objects_.emplace<UnorderedObjects>();
    }
  }

  std::variant<
      SortedObjects,
      UnorderedObjects,
      DynamicUnorderedObjects,
      DynamicSortedObjects>
      objects_;
};

class ObjectStore::Factory {
 public:
  explicit Factory(
      std::shared_ptr<const ObjectScores> scores,
      bool useDynamicObjectOrdering);

  ObjectStore get() const;

 private:
  std::shared_ptr<const ObjectScores> scores_ = nullptr;
  bool useDynamicObjectOrdering_ = true;
};

class ObjectStore::Iterator
    : public std::iterator<std::input_iterator_tag, entities::ObjectId> {
 public:
  explicit Iterator(SortedObjects::const_iterator inner) : inner_(inner) {}
  explicit Iterator(UnorderedObjects::const_iterator inner) : inner_(inner) {}
  explicit Iterator(DynamicUnorderedObjects::const_iterator inner)
      : inner_(inner) {}
  explicit Iterator(DynamicSortedObjects::const_iterator inner)
      : inner_(inner) {}

  inline Iterator& operator++() {
    std::visit([](auto&& inner) { ++inner; }, inner_);
    return *this;
  }

  inline entities::ObjectId operator*() const {
    return std::visit([](auto&& inner) { return *inner; }, inner_);
  }

  inline bool operator!=(const Iterator& other) const {
    return std::visit(
        [&other](auto&& inner) {
          using InnerType = std::decay_t<decltype(inner)>;
          return (inner != std::get<InnerType>(other.inner_));
        },
        inner_);
  }

  inline bool operator==(const Iterator& other) const {
    return std::visit(
        [&other](auto&& inner) {
          using InnerType = std::decay_t<decltype(inner)>;
          return (inner == std::get<InnerType>(other.inner_));
        },
        inner_);
  }

 private:
  std::variant<
      SortedObjects::const_iterator,
      UnorderedObjects::const_iterator,
      DynamicUnorderedObjects::const_iterator,
      DynamicSortedObjects::const_iterator>
      inner_;
};
} // namespace facebook::rebalancer
