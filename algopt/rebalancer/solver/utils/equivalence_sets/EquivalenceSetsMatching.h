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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"

#include <folly/container/F14Map.h>
#include <folly/lang/Hint.h>

#include <compare>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace interface {
class EquivalenceSetsMatching {
  // Given two sets of equivalence sets, our goal is to find matching
  // the from the one equivalent sets to other. This function implements
  // the heavy edge matching algorithm where the weight of the
  // similarity between two sets are computed using Jaccard similarity.
  // If the two equivalence sets are not too different, this is quite close
  // to optimal matching, ref:
  // https://epubs.siam.org/doi/epdf/10.1137/15M1026304
  // returns a map of "names from left -> names from right" This mapping might
  // be incomplete,
 public:
  explicit EquivalenceSetsMatching(
      const EquivalenceSetInfo& equivalenceSetsLeft,
      const EquivalenceSetInfo& equivalenceSetsRight);
  folly::F14FastMap<std::string, std::string> heavyEdgeMatching();

 private:
  // This edge represents a node on the right side equivalence set and its
  // corresponding weight (with respect to a node on the left).
  struct WeightedEdge {
    explicit WeightedEdge(double weight, std::string name)
        : weight_(weight), name_(std::move(name)) {}

    bool operator<(const WeightedEdge& other) const {
      if (weight_ == other.weight_) {
        return name_ < other.name_;
      }
      return weight_ < other.weight_;
    }
    double weight_;
    std::string name_;
  };
  // This struct represents a vertex (equivalence set) on the left side of the
  // bipartite graph. It contains the name the equivalence set and a priority
  // queue of all right side sets connected to it ordered by
  // descending order (priority_queue is a max heap by default) of jacard
  // similarity between the left and right side sets.
  struct BipartiteVertex {
    BipartiteVertex(
        std::string name,
        std::priority_queue<WeightedEdge> connectedEdges)
        : name_(std::move(name)), connectedEdges_(std::move(connectedEdges)) {}

    std::string name_;
    std::priority_queue<WeightedEdge> connectedEdges_;
  };
  const EquivalenceSetInfo& equivalenceSetsLeft_;
  const EquivalenceSetInfo& equivalenceSetsRight_;
  folly::F14FastMap<std::string, std::string> reverseMap;
  std::vector<BipartiteVertex> bipartiteGraph;
  // This function is used to create the reverse map for all the objects in
  // the left equivalence set to their respective sets. The assumption is that
  // the equivalnce sets are mutually exclusive.
  void objectToEquivSetRight();
  // This function is used to create the weighted bipartite graph
  void createBipartiteGraph();
};
} // namespace interface
} // namespace rebalancer
} // namespace facebook
