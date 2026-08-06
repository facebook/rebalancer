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
#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/Function.h>

namespace facebook::rebalancer {

template <class Value>
using ThreadSafeContainer = materializer::SingleEntryCache<Value>;

class ObjectVector : public Expression {
 public:
  // `ObjectValues` is a lightweight handle over shared immutable backing
  // state, so pass/store it by value to keep ownership explicit without
  // copying the underlying object values.
  ObjectVector(
      entities::ObjectValues objectValues,
      const entities::Universe& universe);

  void updateEquivalenceSets(EquivalenceSets&) const override;
  void passiveUpdateEquivalenceSets(EquivalenceSets& eqSets) const;

  bool has_negative_values() const;

  // Given containers Xs
  // return a Map, key is equivalenceSets idx,
  // value is the number of objects with a non-zero value according to this
  // ObjectVector that are initially placed in the given container in this set.
  PackerMap<entities::EquivalenceSetId, int> getEquivSetCount(
      const Problem& problem,
      const PackerSet<entities::ContainerId>& containers) const;

  double getObjectValue(entities::ObjectId objectId) const;

  PackerMap<entities::EquivalenceSetId, double> computeEquivSetMap(
      const EquivalenceSets& equivalenceSets) const;

  bool isZeroDefault() const;
  size_t nonDefaultCount() const;
  size_t totalObjectCount() const;
  void forEachNonDefaultEntry(
      folly::FunctionRef<void(entities::ObjectId, double)> fn) const;
  std::shared_ptr<const entities::Set<entities::ObjectId>> nonDefaultObjectIds()
      const;

  ExpressionProperties getProperties() const override;

  using Expression::references;
  bool references(entities::ObjectId object) const override;

  virtual const std::string_view& getType() const override;

  virtual bool hasNoLpIntent() const override;

  bool inner_is_integer(Context& context) override;

 protected:
  virtual bool hasValue() const override;

 private:
  entities::ObjectValues objectValues_;
  double defaultValue_;
  size_t totalObjects_;
  double lowerBound_;
  double upperBound_;
  bool hasNegativeValues_;
  // true if all the object values are integers
  bool allIntegerValues_;

  mutable ThreadSafeContainer<
      std::shared_ptr<entities::Set<entities::ObjectId>>>
      nonDefaultObjectIds_;

  std::string innerDigest(size_t maxChildren = 10) const override;

  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const override;
};
} // namespace facebook::rebalancer
