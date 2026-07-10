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
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingLatencyMetrics.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingTrafficMetrics.h"
#include "algopt/rebalancer/solver/summary/metrics/Metrics.h"
#include "algopt/rebalancer/solver/summary/metrics/ScopeItemUtilMetrics.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

namespace facebook::rebalancer::explorer {

// Inputs for tabulating a metric collection into a `Table`: the universe for
// name resolution plus the two assignments (as change sets) to compare.
struct TabulateConfig {
  const entities::Universe& universe;
  const Orchestrator& orchestrator;
  const ChangeSet& changeSetA;
  const ChangeSet& changeSetB;
};

Table tabulate(
    const ScopeItemUtilMetrics& metrics,
    const TabulateConfig& config);
Table tabulate(
    const GroupRoutingTrafficMetrics& metrics,
    const TabulateConfig& config);
Table tabulate(
    const GroupRoutingLatencyMetrics& metrics,
    const TabulateConfig& config);

// Dispatches to the tabulate() overload matching `type`.
Table tabulateMetricCollection(
    const Metrics& metrics,
    interface::thrift::MetricCollectionType type,
    const TabulateConfig& config);

} // namespace facebook::rebalancer::explorer
