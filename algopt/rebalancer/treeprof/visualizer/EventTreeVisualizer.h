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

#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/visualizer/VisualizationFilter.h"

#include <folly/container/F14Map.h>
#include <folly/lang/Hint.h>

#include <memory>
#include <string>
#include <vector>

namespace facebook::algopt::treeprof {

class EventTreeVisualizer {
 public:
  enum MetricType { RUNTIME = 0, MEMORY = 1 };
  explicit EventTreeVisualizer(
      std::shared_ptr<const Event> root,
      std::shared_ptr<VisualizationFilter> filter = nullptr);

  /** returns a representation of the hierarchy tree using
   * Unicode characters **/

  std::string digest(
      MetricType type = MetricType::RUNTIME,
      bool dontShowValues = false);

  std::string digest(
      std::shared_ptr<const Event> node,
      MetricType type,
      std::string prefix,
      bool dontShowValues) const;

  const folly::F14FastMap<std::shared_ptr<const Event>, bool>&
  getEventVisibility() const;

 private:
  /** returns the human readable metric value used for visualization such as "X
   MBs" for memory; "Z seconds" for time */
  static std::string displayMetric(double metricValue, MetricType type);

  /* returns the maximum value of the metric */
  double getNormalizer(MetricType type) const;

  /** returns true if @param node clears all filters*/
  bool clearsAllFilters(std::shared_ptr<const Event> node) const;

  /** returns the metric (runtime or memory) of @param node */
  double getMetricExclusive(std::shared_ptr<const Event> node, MetricType type)
      const;
  static double getMetricInclusive(
      std::shared_ptr<const Event> node,
      MetricType type);

  static double computeAggregatedRuntime(
      const std::vector<std::shared_ptr<const Event>>& children);
  static double computeAggregatedMemory(
      const std::vector<std::shared_ptr<const Event>>& children);

  /** Recursilvely initialize visibility of nodes by applying filters : node is
   visible if it clears the filters OR has at least one visible child
   returns true if @param node is visible */
  bool initVisibility(std::shared_ptr<const Event> node);

  std::shared_ptr<const Event> root_;
  std::shared_ptr<VisualizationFilter> filter_;
  double runtimeNormalizer_;
  double memoryNormalizer_;
  folly::F14FastMap<std::shared_ptr<const Event>, bool> isVisible_;
};

} // namespace facebook::algopt::treeprof
