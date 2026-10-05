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

#include "algopt/rebalancer/treeprof/visualizer/EventTreeVisualizer.h"

#include "algopt/rebalancer/treeprof/visualizer/VisualizationFilter.h"

#include <fmt/core.h>
#include <folly/ConstexprMath.h>
#include <folly/container/F14Map.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/container/irange.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/String.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <string_view>
#include <tuple>
#include <utility>

namespace facebook::algopt::treeprof {

namespace {

char colDelimiter = '\001';

double percentNormalizedValue(double value, double normalizer) {
  return (value * 100) / normalizer;
}

// dynamically choses the right unit KBs, MBs, GBs, etc.
std::string makeHumanReadable(int64_t numBytes) {
  return folly::prettyPrint(numBytes, folly::PrettyType::PRETTY_BYTES);
}

/** returns some additional info related to the metric such as memory delta */
std::string getAdditionalInfoStr(
    std::shared_ptr<const Event> node,
    EventTreeVisualizer::MetricType type) {
  if (type == EventTreeVisualizer::MetricType::MEMORY) {
    return fmt::format(", {} delta", makeHumanReadable(node->getMemoryDelta()));
  }
  return "";
}

} // namespace

EventTreeVisualizer::EventTreeVisualizer(
    std::shared_ptr<const Event> root,
    std::shared_ptr<VisualizationFilter> filter)
    : root_(root),
      filter_(std::move(filter)),
      runtimeNormalizer_(root->duration()),
      memoryNormalizer_(root->getMemoryPeak()) {
  initVisibility(std::move(root));
}

std::string EventTreeVisualizer::digest(
    std::shared_ptr<const Event> node,
    MetricType type,
    std::string prefix,
    bool dontShowValues) const {
  std::stringstream ss;
  auto metricValue = getMetricInclusive(node, type);
  const auto percentWeightRaw =
      percentNormalizedValue(metricValue, getNormalizer(type));
  const auto percentWeightInt = std::isnan(percentWeightRaw)
      ? 0
      : static_cast<int>(std::ceil(percentWeightRaw));
  std::string percentStr;
  for ([[maybe_unused]] const auto _ : folly::irange(percentWeightInt)) {
    percentStr += "=";
  }
  std::string exclContributionStr;
  auto valExclusive = getMetricExclusive(node, type);
  if (!node->getChildren().empty() && valExclusive > 1e-3) {
    exclContributionStr =
        fmt::format(", {} excl.", displayMetric(valExclusive, type));
  }
  percentStr += fmt::format(
      " [{} total{}{}]",
      displayMetric(metricValue, type),
      exclContributionStr,
      getAdditionalInfoStr(node, type));

  ss << fmt::format(
      "{} {}{}\n",
      node->getName(),
      colDelimiter,
      dontShowValues ? "" : percentStr);
  std::vector<std::shared_ptr<const Event>> qualifiedChildren;
  const auto& children = node->getChildren();
  qualifiedChildren.reserve(children.size());
  for (const auto& child : children) {
    if (isVisible_.at(child)) {
      qualifiedChildren.push_back(child);
    }
  }
  if (!qualifiedChildren.empty()) {
    std::stringstream ss2;
    ss2 << prefix << "   ";
    auto maxChildIndex = qualifiedChildren.size() - 1;
    for (const auto j : folly::irange(maxChildIndex + 1)) {
      const auto& child = qualifiedChildren.at(j);
      auto indentMarker = (j < maxChildIndex) ? "├─" : "└─";
      auto depthMarker = (j < maxChildIndex) ? "│" : " ";
      ss << ss2.str() << indentMarker;
      ss << digest(child, type, ss2.str() + depthMarker, dontShowValues);
    }
  }
  return ss.str();
}

std::string EventTreeVisualizer::digest(MetricType type, bool dontShowValues) {
  // 1. Extract the digest string by recursive traversal
  auto digestStr = digest(root_, type, "", dontShowValues);
  // 2. Postprocess the string so that fields are correctly aligned
  std::vector<std::string_view> lines;
  lines.reserve(50);
  folly::split('\n', digestStr, lines, true);
  int maxDescLength = 0;
  std::vector<std::tuple<std::string, std::string>> visualizationPerLine;
  visualizationPerLine.reserve(lines.size());
  for (auto& line : lines) {
    std::vector<std::string_view> fields;
    fields.reserve(2);
    folly::split(colDelimiter, line, fields);
    if (fields.size() != 2) {
      XLOG(ERR) << "Malformed line for Hierarchytree::digest() "
                << fields.size() << ":" << line;
      continue;
    }
    if (fields.at(0).length() > maxDescLength) {
      maxDescLength = fields.at(0).length();
    }
    visualizationPerLine.emplace_back(fields.at(0), fields.at(1));
  }
  std::stringstream ss;
  for (auto& [desc, visualization] : visualizationPerLine) {
    ss << fmt::format("{:·<{}}", desc, maxDescLength)
       << fmt::format("│{}\n", visualization);
  }
  // add an extra newline for clarity
  ss << std::endl;
  return ss.str();
}

const folly::F14FastMap<std::shared_ptr<const Event>, bool>&
EventTreeVisualizer::getEventVisibility() const {
  return isVisible_;
}

bool EventTreeVisualizer::clearsAllFilters(
    std::shared_ptr<const Event> node) const {
  if (filter_) {
    // filter_ maybe a logical combination of many filters
    return filter_->apply(std::move(node));
  }
  return true;
}

double EventTreeVisualizer::getNormalizer(MetricType type) const {
  if (type == MetricType::RUNTIME) {
    return runtimeNormalizer_;
  } else if (type == MetricType::MEMORY) {
    return memoryNormalizer_;
  } else {
    throw std::runtime_error("unsuported metric type");
  }
}

std::string EventTreeVisualizer::displayMetric(
    double metricValue,
    MetricType type) {
  if (type == MetricType::RUNTIME) {
    return fmt::format("{:.2f}s", metricValue);
  } else if (type == MetricType::MEMORY) {
    return makeHumanReadable(metricValue);
  } else {
    throw std::runtime_error("unsuported metric type");
  }
}

double EventTreeVisualizer::getMetricInclusive(
    std::shared_ptr<const Event> node,
    MetricType type) {
  if (type == MetricType::RUNTIME) {
    return node->duration();
  } else if (type == MetricType::MEMORY) {
    return node->getMemoryPeak();
  }
  throw std::runtime_error("unexpected metric type");
}

double EventTreeVisualizer::getMetricExclusive(
    std::shared_ptr<const Event> node,
    MetricType type) const {
  std::vector<std::shared_ptr<const Event>> visibleChildren;
  for (auto& child : node->getChildren()) {
    if (isVisible_.at(child)) {
      visibleChildren.push_back(child);
    }
  }

  auto accountedByVisibleChildren = type == MetricType::RUNTIME
      ? computeAggregatedRuntime(visibleChildren)
      : computeAggregatedMemory(visibleChildren);

  return getMetricInclusive(std::move(node), type) - accountedByVisibleChildren;
}

double EventTreeVisualizer::computeAggregatedRuntime(
    const std::vector<std::shared_ptr<const Event>>& children) {
  // child events may be overlapping, if they were disjoint, we could simply add
  // up duration for each child event and return that value. In the following
  // we compute the duration spanned where at least one child was active

  // A point represents an interval endpoint
  // Format: {time, is_start_endpoint}
  std::vector<std::pair<double, bool>> endPoints;
  for (const auto& child : children) {
    endPoints.emplace_back(child->getBeginTime(), true);
    endPoints.emplace_back(child->getEndTime(), false);
  }
  std::sort(endPoints.begin(), endPoints.end());

  int numRunningEvents = 0;
  double lastEventPointTime = 0;
  double childActiveDuration = 0;
  for (auto ep : endPoints) {
    auto [eventTimeVal, isStart] = ep;
    if (numRunningEvents > 0) {
      childActiveDuration += eventTimeVal - lastEventPointTime;
    }
    if (isStart) {
      numRunningEvents++;
    } else {
      numRunningEvents--;
    }
    lastEventPointTime = eventTimeVal;
  }
  return childActiveDuration;
}

double EventTreeVisualizer::computeAggregatedMemory(
    const std::vector<std::shared_ptr<const Event>>& children) {
  // this is very similar to how we compute peak memory and delta of an event
  // See function Event::finalize()
  // except we work on the granularity of provided list of children
  // A point represents a memory usage increase or decrease at a given time DUE
  // TO a child \in children
  // Format: {time, memory delta}
  std::vector<std::pair<double, int64_t>> points;
  for (const auto& child : children) {
    // Assumption: peak memory is attained at the start of child interval
    points.emplace_back(child->getBeginTime(), child->getMemoryPeak());
    points.emplace_back(
        child->getEndTime(), child->getMemoryDelta() - child->getMemoryPeak());
  }
  // Sort points increasingly by time.
  std::sort(points.begin(), points.end());
  int64_t memoryDelta = 0;
  int64_t memoryPeak = 0;
  for (auto& [_, delta] : points) {
    memoryDelta += delta;
    memoryPeak = std::max(memoryPeak, memoryDelta);
  }
  return memoryPeak;
}

bool EventTreeVisualizer::initVisibility(std::shared_ptr<const Event> node) {
  bool thisNodeVisible = clearsAllFilters(node);
  for (const auto& child : node->getChildren()) {
    const bool childVisible = initVisibility(child);
    if (!thisNodeVisible && childVisible) {
      thisNodeVisible = true;
    }
  }
  isVisible_[node] = thisNodeVisible;
  return thisNodeVisible;
}

} // namespace facebook::algopt::treeprof
