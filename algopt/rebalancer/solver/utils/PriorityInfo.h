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

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/Optional.h>

#include <cstddef>

namespace facebook::rebalancer {

struct PriorityInfo {
  bool operator>(const PriorityInfo& other) const {
    // (r1, h1) has a higher priority than (r2, h2) if r1 < r2; if r1 == r2,
    // then prefer the one with the lower height
    return (rootIdx == other.rootIdx) ? height < other.height
                                      : rootIdx < other.rootIdx;
  }

  bool operator==(const PriorityInfo& other) const {
    return rootIdx == other.rootIdx && height == other.height;
  }

  bool operator<(const PriorityInfo& other) const {
    return other > *this;
  }

  // for each node, this is the earliest root under which the node is
  // located
  std::size_t rootIdx = 0;
  // Height of a node is max(children heights) + 1, leaf nodes have height 1
  // Computed during post-order traversal to ensure correctness
  std::size_t height = 1;
};

} // namespace facebook::rebalancer
