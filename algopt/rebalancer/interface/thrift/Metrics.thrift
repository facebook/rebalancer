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

@hack.NamePrefix{prefix = "rebalancer_interface_thrift_"}
@hack.LegacyOmitPrefixInNameString
package "meta.com/algopt/rebalancer"

namespace hack ""

include "algopt/rebalancer/interface/thrift/ProblemSpecs.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

namespace cpp2 facebook.rebalancer.interface.thrift
namespace py rebalancer.interface.thrift.Types
namespace py3 rebalancer.interface.thrift

enum MetricCollectionType {
  SCOPE_ITEM_UTILIZATION_VALUES = 0,
  GROUP_ROUTING_LATENCY_VALUES = 1,
  GROUP_ROUTING_TRAFFIC_VALUES = 2,
}

struct GroupUtils {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, double> groupToValue;
}

struct PartitionUtils {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, GroupUtils> partitionToGroupUtils;
}

@thrift.ReserveIds{ids = [1]}
struct ScopeItemUtils {
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, double> scopeItemToValue;

  // if available, for each scope item, the field below contains utilization of each group in the scopeItem
  @cpp.Type{template = "folly::F14FastMap"}
  3: map<string, PartitionUtils> scopeItemToPartitionUtils;
}

struct DimensionUtils {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, ScopeItemUtils> dimensionToScopeItemUtils;
}

struct ScopeUtils {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, DimensionUtils> scopeToDimensionUtils;
}

struct SourceTrafficMetrics {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, ScopeItemUtils> sourceToDestinationTraffic;
}

@thrift.ReserveIds{ids = [1]}
struct GroupTrafficMetrics {
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, SourceTrafficMetrics> groupToSourceTraffic;
}

struct LatencyMetricValue {
  1: ProblemSpecs.RoutingLatencyMetricInfo metric;
  2: double value;
}

struct GroupLatencyMetrics {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, list<LatencyMetricValue>> groupToMetricValues;
}

@thrift.ReserveIds{ids = [2, 3]}
struct Metrics {
  /**
  If available, for each util metric (like "after", "during"; see https://fburl.com/wiki/mm2kxmsp), 'utilMetricToScopeUtils' map
  specifies the ScopeUtils.   ScopeUtils is in turn a map from each scope S to all DimensionVals, where DimensionVals is map from
  a dimension D to ScopeItemUtils. ScopeItemUtils specifies a map from each scopeItem I in S to
  a) the utilization of I w.r.t. D
  b) the utilization w.r.t. D of each group G of a partition P in scope item I (as specified through PartitionUtils)

  Note that the values in a) and b) are only available if the corresponding expressions were explicitly computed durin the solve.

  Example usage: to get "after" util of scopeItem "region1" in scope "region" w.r.t. "load" dimension:
  metrics.utilMetricToScopeUtils()->at("after").scopeToDimensionUtils()->at("region").dimensionToScopeItemUtils()
  ->at("load").scopeItemToValue()->at("region1").

  Example usage: to get "after" util of group "job1" in partition "job" w.r.t. scopeItem "region1" in scope "region"  and "load" dimension:
  metrics.utilMetricToScopeUtils()->at("after").scopeToDimensionUtils()->at("region").dimensionToScopeItemUtils()
  ->at("load").scopeItemToPartitionUtils()->at("region1").partitionToGroupUtils()->at("job").groupToValue()->at("job1")

  */
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, ScopeUtils> utilMetricToScopeUtils;

  /**
  For each routingConfig, 'routingConfigToGroupTrafficMetrics' map specifies GroupToSourceTraffic values. GroupToSourceTraffic in turn is map from a group
  to a map which specifies the traffic from source scopeItems to each relevant destination. Given a group G and a source scopeItem S, this map specifies the total
  amount of traffic sent from S to each relevant destination scopeItem. If a destination is not present in the map associated with a source scopeItem S,
  then the traffic to it from S is zero.

  Example usage: to get the amount of "tenant1"'s traffic sent from "region0" to "region1" for routingConfig "config":
  metrics.routingConfigToGroupTrafficMetrics()->at("config").groupToSourceTraffic()->at("tenant1").sourceToDestinationTraffic()->at("region0").scopeItemToValue()->at("region1");
  */
  @cpp.Type{template = "folly::F14FastMap"}
  4: map<string, GroupTrafficMetrics> routingConfigToGroupTrafficMetrics;

  /**
  For each routingConfig, 'routingConfigToGroupLatencyVals' map specifies the GroupToLatencyMetricValues. LatencyMetricValues is in turn
  is list of LatencyMetricValue each of which contains the latency metric and the value w.r.t. that metric.

  Example usage: latency metric values of group "tenant1" for routingConfig "config":
    metrics.routingConfigToGroupLatencyMetrics()->at("config").groupToMetricValues()["tenant1"]  // this gives all the metrics w.r.t. "tenant1"
  */
  @cpp.Type{template = "folly::F14FastMap"}
  5: map<string, GroupLatencyMetrics> routingConfigToGroupLatencyMetrics;
}
