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

#include "algopt/lp/generic/Expression.h"
#include "algopt/rebalancer/algopt_common/BucketedPriorityQueue.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/PriorityInfo.h"
#include "algopt/rebalancer/solver/utils/TrafficTable.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/lang/Hint.h>
#include <folly/Optional.h>

#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stdint.h>
#include <utility>

namespace facebook::rebalancer {

template <class Key, class Value>
using ThreadSafeCache = rebalancer::materializer::Cache<Key, Value>;
using ExprId = int64_t;

template <class T>
class Cache {
 public:
  void clear() {
    hits = 0;
    total = 0;
    cache.clear();
  }
  std::optional<T> get(ExprId exp_id) {
    total++;
    const auto& it = cache.find(exp_id);
    if (it != cache.end()) {
      hits++;
      return {it->second};
    }
    return {};
  }

  const T& at(ExprId exp_id) const {
    const auto& it = cache.find(exp_id);
    if (it != cache.end()) {
      return it->second;
    }
    throw std::runtime_error("exp_id not found in cache");
  }

  bool contains(ExprId exp_id) const {
    const auto& it = cache.find(exp_id);
    if (it != cache.end()) {
      return true;
    }
    return false;
  }

  T save(ExprId exp_id, const T& val) {
    cache.insert(std::make_pair(exp_id, val));
    return val;
  }
  int hits = 0;
  int total = 0;
  PackerMap<ExprId, T> cache;
};

class Expression;
class ObjectVector;

using BucketedPriorityQueue =
    algopt::BucketedPriorityQueue<PriorityInfo, Expression*>;

// Context is NOT thread-safe; only members that are used in LpContext are
// thread-safe.
// TODO: make Context thread-safe
class Context {
 public:
  virtual ~Context() = default;

  ThreadSafeCache<ExprId, bool>& binary() {
    return binary_;
  }

  ThreadSafeCache<ExprId, bool>& integer() {
    return integer_;
  }

  ThreadSafeCache<ExprId, rebalancer::Bounds>& bounds() {
    return bounds_;
  }

  virtual Cache<double>& apply() {
    return apply_;
  }

  virtual Cache<double>& val() {
    return val_;
  }

  virtual Cache<bool>& isPositive() {
    return isPositive_;
  }

  virtual PackerMap<const Expression*, folly::F14FastSet<Expression*>>&
  changedChildren() {
    return changedChildren_;
  }

  virtual folly::Optional<ChangeSet>& changes() {
    return changes_;
  }

  virtual BucketedPriorityQueue& readyNodes() {
    return readyNodes_;
  }

  virtual Cache<
      TrafficTableWithStats<entities::ScopeItemId, entities::ScopeItemId>>&
  groupToTempTrafficTable() {
    return groupToTempTrafficTable_;
  }

  // evaluatedRoots indicates the index of roots having been pushed to
  // readyNodes
  std::atomic<int> evaluatedRoots = -1;

  virtual void clear() {
    bounds_.clear();
    val_.clear();
    apply_.clear();
    isPositive_.clear();
    changes_.reset();
    changedChildren_.clear();
    readyNodes_.clear();
    groupToTempTrafficTable_.clear();

    evaluatedRoots = -1;
  }

 private:
  Cache<double> val_;
  Cache<double> apply_;
  Cache<bool> isPositive_;
  PackerMap<const Expression*, folly::F14FastSet<Expression*>> changedChildren_;
  folly::Optional<ChangeSet> changes_;
  BucketedPriorityQueue readyNodes_;

  // used to store the temp traffic table that is created during evaluating
  // changes in GroupRoutingRing expression
  Cache<TrafficTableWithStats<entities::ScopeItemId, entities::ScopeItemId>>
      groupToTempTrafficTable_;

  // the following members are also used in LpContext
  ThreadSafeCache<ExprId, bool> binary_;
  ThreadSafeCache<ExprId, bool> integer_;
  ThreadSafeCache<ExprId, Bounds> bounds_;
};

class LpContext : public Context {
 public:
  LpContext(
      PackerSet<entities::ContainerId> dynamicContainers,
      PackerSet<entities::EquivalenceSetId> dynamicEquivSets,
      folly::F14FastMap<Expression*, folly::F14FastSet<Expression*>>
          dynamicChildren)
      : dynamicContainers_{std::move(dynamicContainers)},
        dynamicEquivSets_{std::move(dynamicEquivSets)},
        dynamicChildren_{std::move(dynamicChildren)} {}

  const PackerSet<entities::ContainerId>& dynamicContainers() const {
    return dynamicContainers_;
  }

  const PackerSet<entities::EquivalenceSetId>& dynamicEquivSets() const {
    return dynamicEquivSets_;
  }

  const folly::F14FastMap<Expression*, folly::F14FastSet<Expression*>>&
  dynamicChildren() const {
    return dynamicChildren_;
  }

  ThreadSafeCache<ExprId, algopt::lp::Expression>& lpMin() {
    return lpMin_;
  }

  ThreadSafeCache<ExprId, algopt::lp::Expression>& lpNotMin() {
    return lpNotMin_;
  }

  ThreadSafeCache<
      entities::ContainerId,
      PackerMap<entities::EquivalenceSetId, int>>&
  lpFixedVals() {
    return lpFixedVals_;
  }

  const PackerMap<entities::EquivalenceSetId, double>& getEquivSetMap(
      ObjectVector* objVec,
      const EquivalenceSets& equivalenceSets);

 private:
  ThreadSafeCache<ExprId, algopt::lp::Expression> lpMin_;
  ThreadSafeCache<ExprId, algopt::lp::Expression> lpNotMin_;
  ThreadSafeCache<ExprId, PackerMap<entities::EquivalenceSetId, double>>
      objVectorExprIdToEquivSetToValue_;
  ThreadSafeCache<
      entities::ContainerId /* container */,
      PackerMap<entities::EquivalenceSetId, int /* value */>>
      lpFixedVals_;

  // read only data members
  const PackerSet<entities::ContainerId> dynamicContainers_;
  const PackerSet<entities::EquivalenceSetId> dynamicEquivSets_;
  const folly::F14FastMap<Expression*, folly::F14FastSet<Expression*>>
      dynamicChildren_;

 private:
  virtual Cache<double>& apply() override {
    throw std::runtime_error("apply() cannot be used with LpContext");
  }

  virtual Cache<double>& val() override {
    throw std::runtime_error("val() cannot be used with LpContext");
  }

  virtual Cache<bool>& isPositive() override {
    throw std::runtime_error("isPositive() cannot be used with LpContext");
  }

  virtual PackerMap<const Expression*, folly::F14FastSet<Expression*>>&
  changedChildren() override {
    throw std::runtime_error("changedChildren() cannot be used with LpContext");
  }

  virtual folly::Optional<ChangeSet>& changes() override {
    throw std::runtime_error("changes() cannot be used with LpContext");
  }

  virtual BucketedPriorityQueue& readyNodes() override {
    throw std::runtime_error("readyNodes() cannot be used with LpContext");
  }

  virtual void clear() override {
    throw std::runtime_error(
        "clear() is not currently supported with LpContext");
  }
};

} // namespace facebook::rebalancer
