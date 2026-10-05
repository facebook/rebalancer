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
#include "algopt/rebalancer/entities/ObjectValueTypes.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/F14Set.h>
#include <folly/lang/Hint.h>

#include <iterator>
#include <stddef.h>
#include <stdint.h>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

// TODO: strongly type ExprId like ObjectId/ContainerId in
// algopt/rebalancer/entities/Identifiers.h.
using ExprId = int64_t;

class EquivalenceSets {
 public:
  explicit EquivalenceSets(const entities::Universe& universe);

  size_t size() const;
  const PackerSet<entities::ObjectId>& getSet(
      entities::EquivalenceSetId idx) const;
  entities::EquivalenceSetId at(entities::ObjectId element) const;
  void print() const;

  template <typename InputContainer>
  void combine(const InputContainer& input);

  template <class ObjectToValueMap>
  void mappingMerge(const ObjectToValueMap& mapping);

  template <class ObjectToValueMap>
  void mappingMerge(ExprId exprId, const ObjectToValueMap& mapping);

  void mappingMerge(const entities::ObjectValues& values);
  void mappingMerge(ExprId exprId, const entities::ObjectValues& values);
  void mappingMerge(entities::PartitionId partitionId);
  void mappingMerge(
      entities::PartitionId partitionId,
      entities::GroupId groupId);
  void mappingMerge(entities::DimensionId dimensionId);
  void mappingMerge(
      entities::DimensionId dimensionId,
      entities::ScopeItemId scopeItemId);

  bool hasObject(entities::ObjectId objectId) const;

  // Places every still-unseen object into one new equivalence set.
  void finalize();

  class EquivalenceSetsIterator {
   public:
    explicit EquivalenceSetsIterator(const EquivalenceSets& sets, size_t index);
    bool operator==(const EquivalenceSetsIterator& other) const;
    bool operator!=(const EquivalenceSetsIterator& other) const;
    EquivalenceSetsIterator& operator++();
    entities::EquivalenceSetId operator*() const;
    EquivalenceSetsIterator begin() const;
    EquivalenceSetsIterator end() const;

   private:
    const EquivalenceSets& sets_;
    size_t iterIndex_;
  };
  EquivalenceSetsIterator ids() const;

 private:
  void addToNewSet(const std::vector<entities::ObjectId>& items);

  static constexpr int kUnassignedSetId = -1;

  const entities::Universe* universe_;
  std::vector<PackerSet<entities::ObjectId>> sets_;
  // i-th entry refers to setId associated with ObjectId(i); if the entry is
  // kUnassignedSetId => object is not in any set.
  std::vector<int> objectIdToSetId_;
  size_t seenObjects_ = 0;

  folly::F14FastSet<ExprId> seenExprIds_;
  folly::F14FastSet<std::pair<entities::PartitionId, entities::GroupId>>
      seenPartitionGroups_;
  folly::F14FastSet<entities::DimensionId> seenDimensions_;
  folly::F14FastSet<std::pair<entities::DimensionId, entities::ScopeItemId>>
      seenDimensionScopeItems_;

  // Scratch buffer for combine(), indexed by set id. Each entry holds the
  // subset of input items belonging to that set. A member (not a local) so
  // inner-vector allocations carry across calls; all inner vectors are empty
  // when combine() is entered.
  std::vector<std::vector<entities::ObjectId>> combineInputBySetId_;
};
} // namespace facebook::rebalancer
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets-inl.h"
