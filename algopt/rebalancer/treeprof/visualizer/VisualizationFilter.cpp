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

#include "algopt/rebalancer/treeprof/visualizer/VisualizationFilter.h"

#include "algopt/rebalancer/treeprof/Event.h"

#include <utility>

namespace facebook::algopt::treeprof {

PeakMemoryAtLeastXMBytes::PeakMemoryAtLeastXMBytes(double threshold)
    : threshold_(threshold) {}

bool PeakMemoryAtLeastXMBytes::apply(std::shared_ptr<const Event> event) {
  return event->getMemoryPeak() >= threshold_ * NUM_BYTES_IN_ONE_MB;
}

RuntimeAtLeastX::RuntimeAtLeastX(double threshold) : threshold_(threshold) {}

bool RuntimeAtLeastX::apply(std::shared_ptr<const Event> event) {
  return event->duration() >= threshold_;
}

EventNameHasWord::EventNameHasWord(std::string word) : word_(std::move(word)) {}

bool EventNameHasWord::apply(std::shared_ptr<const Event> event) {
  return event->getName().find(word_) != std::string::npos;
}

Not::Not(std::shared_ptr<VisualizationFilter> filter)
    : filter_(std::move(filter)) {}
bool Not::apply(std::shared_ptr<const Event> event) {
  return !filter_->apply(std::move(event));
}

And::And(
    std::shared_ptr<VisualizationFilter> first,
    std::shared_ptr<VisualizationFilter> second)
    : first_(std::move(first)), second_(std::move(second)) {}

bool And::apply(std::shared_ptr<const Event> event) {
  return first_->apply(event) && second_->apply(std::move(event));
}

Or::Or(
    std::shared_ptr<VisualizationFilter> first,
    std::shared_ptr<VisualizationFilter> second)
    : first_(std::move(first)), second_(std::move(second)) {}

bool Or::apply(std::shared_ptr<const Event> event) {
  return first_->apply(event) || second_->apply(std::move(event));
}

} // namespace facebook::algopt::treeprof
