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

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Max.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"

#include <fmt/format.h>
#include <folly/Synchronized.h>
#include <folly/Utility.h>

namespace facebook::rebalancer {

class MetricCollection {
 public:
  void applyAndAddToSummary(
      const Assignment& assignment,
      Context& context,
      const entities::Universe& universe,
      interface::thrift::Metrics& metricsSummary) const {
    fullApply(assignment, context);
    addToSummary(universe, metricsSummary);
  }

  virtual void fullApply(const Assignment& assignment, Context& context)
      const = 0;

  virtual interface::thrift::MetricCollectionType getType() const = 0;

  virtual void pushRootTo(std::vector<Expression*>& exprs) const = 0;

  virtual void buildRootExpr(
      std::shared_ptr<const entities::Universe> universe) = 0;

  virtual Expression* getRootExprRawPtr() const = 0;

  virtual ~MetricCollection() = default;

  static std::string toString(materializer::UtilMetric utilMetric);

 protected:
  virtual void addToSummary(
      const entities::Universe& universe,
      interface::thrift::Metrics& metricsSummary) const = 0;

  static void throwOnInsertFailure(
      bool insertSuccess,
      const std::string& message);
};

template <typename Key, typename Value = ExprPtr>
class MetricCollectionImpl : public MetricCollection {
 public:
  void fullApply(const Assignment& assignment, Context& context)
      const override {
    getRootExprRawPtr()->fullApply(TopToBottomEvaluator(context), assignment);
  }

  void pushRootTo(std::vector<Expression*>& exprs) const override {
    exprs.emplace_back(getRootExprRawPtr());
  }

  void buildRootExpr(
      std::shared_ptr<const entities::Universe> universe) override {
    // collect all the expressions from innerCollection_ and build a root
    // expression; root expression can be any aggregate expression that combines
    // all the expressions in the collection
    const auto rLockedCollection = innerCollection_.rlock();
    std::vector<ExprPtr> exprs;
    exprs.reserve(rLockedCollection->size());
    std::transform(
        rLockedCollection->begin(),
        rLockedCollection->end(),
        std::back_inserter(exprs),
        [](const auto& pair) { return pair.second; });

    rootExpr_ = std::make_shared<Max>(exprs, *universe);
  }

  Expression* getRootExprRawPtr() const override {
    if (!rootExpr_) {
      throw std::runtime_error("Expected rootExpr_ to be set");
    }

    return rootExpr_.get();
  }

  template <typename Callback>
  void forEachMetricExpressionForTabulation(Callback&& callback) const {
    const auto rLockedCollection = innerCollection_.rlock();
    for (const auto& [key, value] : *rLockedCollection) {
      callback(key, value);
    }
  }

 protected:
  folly::Synchronized<folly::F14FastMap<Key, Value>> innerCollection_;
  /*
  Root expression is one that combines all the expressions in the collection.
  This is useful to speed-up evaluations. For context, see summary in D77158904.
  */
  ExprPtr rootExpr_ = nullptr;
};

/*static*/
inline std::string MetricCollection::toString(
    materializer::UtilMetric utilMetric) {
  switch (utilMetric) {
    case materializer::UtilMetric::AFTER:
      return "after";
    case materializer::UtilMetric::DURING:
      return "during";
    case materializer::UtilMetric::NEW:
      return "new";
    case materializer::UtilMetric::OLD:
      return "old";
    case materializer::UtilMetric::INITIAL:
      return "initial";
    case materializer::UtilMetric::STAYED:
      return "stayed";
    case materializer::UtilMetric::MOVED:
      return "moved";
    default:
      throw std::runtime_error{fmt::format(
          "unknown toString conversion from given UtilMetric, {}",
          folly::to_underlying(utilMetric))};
  }
}

/*static*/
inline void MetricCollection::throwOnInsertFailure(
    bool insertSuccess,
    const std::string& message) {
  if (!insertSuccess) {
    throw std::runtime_error(message);
  }
}

} // namespace facebook::rebalancer
