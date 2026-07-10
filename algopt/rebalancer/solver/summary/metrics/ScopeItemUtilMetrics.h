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
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/summary/metrics/MetricCollection.h"

namespace facebook::rebalancer {

using ScopeItemUtilKey = std::tuple<
    materializer::UtilMetric,
    entities::ScopeId,
    entities::DimensionId,
    entities::ScopeItemId,
    std::optional<entities::PartitionId>,
    std::optional<entities::GroupId>>;

class ScopeItemUtilMetrics : public MetricCollectionImpl<ScopeItemUtilKey> {
 public:
  void add(
      ExprPtr expr,
      materializer::UtilMetric utilMetric,
      const materializer::Descriptor& descriptor);

  void add(
      std::shared_ptr<ObjectPartitionLookupDefault> lookup,
      materializer::UtilMetric utilMetric);

  virtual interface::thrift::MetricCollectionType getType() const override;

 private:
  virtual void addToSummary(
      const entities::Universe& universe,
      interface::thrift::Metrics& metricsSummary) const override;
};

} // namespace facebook::rebalancer
