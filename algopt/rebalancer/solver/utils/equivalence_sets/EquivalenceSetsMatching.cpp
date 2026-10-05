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

#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSetsMatching.h"

#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"

#include <folly/ConstexprMath.h>
#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/lang/Assume.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <algorithm>
#include <stdexcept>

namespace facebook {
namespace rebalancer {
namespace interface {

EquivalenceSetsMatching::EquivalenceSetsMatching(
    const EquivalenceSetInfo& equivalenceSetsLeft,
    const EquivalenceSetInfo& equivalenceSetsRight)
    : equivalenceSetsLeft_(equivalenceSetsLeft),
      equivalenceSetsRight_(equivalenceSetsRight) {
  objectToEquivSetRight();
  createBipartiteGraph();
}

void EquivalenceSetsMatching::objectToEquivSetRight() {
  for (auto& equivSet : *equivalenceSetsRight_.equivalenceSets()) {
    const auto& equivSetName = *equivSet.name();
    for (auto& object : *equivSet.objectNames()) {
      reverseMap.insert({object, equivSetName});
    }
  }
}

void EquivalenceSetsMatching::createBipartiteGraph() {
  // Create the bipartite graph leftSetId -> {weight, rightSetId}

  // iterate over the right equivalence sets and create a map of name to size
  folly::F14FastMap<std::string, double> rightSetSizeMap;
  for (auto& rightEquivSet : *equivalenceSetsRight_.equivalenceSets()) {
    auto& objects = *rightEquivSet.objectNames();
    auto& rightSetName = *rightEquivSet.name();
    const double numObjects = objects.size();
    rightSetSizeMap[rightSetName] = numObjects;
  }

  folly::F14FastMap<std::string, double> leftSetSizeMap;
  for (auto& leftEquivSet : *equivalenceSetsLeft_.equivalenceSets()) {
    auto& objects = *leftEquivSet.objectNames();
    auto& leftSetName = *leftEquivSet.name();
    const double leftSetSize = objects.size();
    leftSetSizeMap[leftSetName] = leftSetSize;
    folly::F14FastMap<std::string, double> connectedSetWeights;
    for (auto& object : objects) {
      auto rightSetName = reverseMap.at(object);
      connectedSetWeights[rightSetName] += 1.0f; // getting the common objects
    }

    // now update the weight from the common objects to Jaccard similarity
    // Jaccard = |A intersect B| / |A union B|
    //         = |A intersect B| / (|A| + |B| - |A intersect B|)
    std::priority_queue<WeightedEdge> weightOrderedEdges;
    for (const auto& [rightSetName, commonObjects] : connectedSetWeights) {
      const double rightSetSize = rightSetSizeMap[rightSetName];
      const double jaccard =
          commonObjects / (leftSetSize + rightSetSize - commonObjects);
      weightOrderedEdges.emplace(jaccard, rightSetName);
    }
    if (!weightOrderedEdges.empty()) {
      bipartiteGraph.emplace_back(leftSetName, std::move(weightOrderedEdges));
    }
  }
  std::sort(
      bipartiteGraph.begin(),
      bipartiteGraph.end(),
      [](const BipartiteVertex& a, const BipartiteVertex& b) {
        // sort the left equivalence sets by heaviest edge.
        if (a.connectedEdges_.empty() || b.connectedEdges_.empty()) {
          std::runtime_error("Nodes with empty connected edges");
        }
        if (a.connectedEdges_.top().weight_ ==
            b.connectedEdges_.top().weight_) {
          return a.name_ < b.name_;
        }
        return a.connectedEdges_.top().weight_ >
            b.connectedEdges_.top().weight_;
      });
}

folly::F14FastMap<std::string, std::string>
EquivalenceSetsMatching::heavyEdgeMatching() {
  // Given two sets of equivalence sets, our goal is to find matching
  // the from the one equivalent sets to other. This function implements
  // the heavy edge matching algorithm where the weight of the
  // similarity between two sets are computed using Jaccard similarity.
  // If the two equivalence sets are not too different, this is quite close
  // to optimal matching, ref:
  // https://epubs.siam.org/doi/epdf/10.1137/15M1026304
  // returns a map of "names from left -> names from right" This mapping might
  // be incomplete.
  folly::F14FastMap<std::string, std::string> matching;
  folly::F14FastSet<std::string> mated;

  // Find the maximum matching using the heavy edge matching
  // algorithm
  for (auto& [leftSetName, weightOrderedEdges] : bipartiteGraph) {
    while (!weightOrderedEdges.empty() &&
           (weightOrderedEdges.top().weight_ <= 0.0f ||
            mated.contains(weightOrderedEdges.top().name_))) {
      weightOrderedEdges.pop();
    }
    if (!weightOrderedEdges.empty()) {
      const auto& [_, rightSetName] = weightOrderedEdges.top();
      matching.insert({leftSetName, rightSetName});
      mated.insert(rightSetName);
    }
  }

  return matching;
}
} // namespace interface
} // namespace rebalancer
} // namespace facebook
