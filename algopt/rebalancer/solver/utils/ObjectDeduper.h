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
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <iterator>
#include <memory>

namespace facebook::rebalancer {

template <class ObjectIterator>
class ObjectDeduperIterator {
 public:
  using iterator_category = std::input_iterator_tag;
  using value_type = entities::ObjectId;
  using difference_type = void;
  using pointer = value_type*;
  using reference = value_type&;

  ObjectDeduperIterator(
      const EquivalenceSets* equivalenceSets,
      ObjectIterator firstObject,
      ObjectIterator lastObject);
  ObjectDeduperIterator& operator++();
  bool operator==(const ObjectDeduperIterator& rhs) const;
  bool operator!=(const ObjectDeduperIterator& rhs) const;
  entities::ObjectId operator*() const;

 private:
  const EquivalenceSets* equivalenceSets_;
  ObjectIterator currentObject_;
  ObjectIterator lastObject_;
  PackerSet<entities::EquivalenceSetId> lookup_;
};

template <class ObjectIterator>
class GenericObjectDeduper {
 public:
  using iterator = ObjectDeduperIterator<ObjectIterator>;
  using const_iterator = iterator;

  GenericObjectDeduper(
      const EquivalenceSets* equivalenceSets,
      ObjectIterator firstObject,
      ObjectIterator lastObject);
  ObjectDeduperIterator<ObjectIterator> begin() const;
  ObjectDeduperIterator<ObjectIterator> end() const;

 private:
  const EquivalenceSets* equivalenceSets_;
  ObjectIterator firstObject_;
  ObjectIterator lastObject_;
};

class ObjectDeduper : public GenericObjectDeduper<ObjectStore::Iterator> {
 public:
  ObjectDeduper(
      const EquivalenceSets* equivalenceSets,
      const ObjectStore& objects);
};
} // namespace facebook::rebalancer

#include "algopt/rebalancer/solver/utils/ObjectDeduper-inl.h"
