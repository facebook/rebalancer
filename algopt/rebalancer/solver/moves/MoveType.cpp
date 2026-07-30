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

#include "algopt/rebalancer/solver/moves/MoveType.h"

#include "algopt/rebalancer/solver/moves/DestinationsToExploreGenerator.h"
#include "algopt/rebalancer/solver/moves/ObjectsToExploreGenerator.h"
#include "algopt/rebalancer/solver/utils/MovesSummaryHelper.h"

#include <folly/logging/xlog.h>

#include <algorithm>
#include <stdexcept>

namespace facebook::rebalancer {

int MoveType::getBundleSize(
    const interface::ObjectsToExploreOptions& exploreOptions) {
  auto type = exploreOptions.getType();
  switch (type) {
    case interface::ObjectsToExploreOptions::Type::objectsFromGroupsSpec:
      return exploreOptions.get_objectsFromGroupsSpec().bundleSize().value_or(
          1);
    default:
      return 1;
  }
}

MoveType::~MoveType() = default;

MoveResult MoveType::findBestWithValidator(
    Problem& problem,
    std::vector<MoveResult> results,
    const MoveStatsAggregator& stats) {
  const auto& precision = problem.getUniverse().getPrecision();
  // sort by best value, will go in order to find first valid move
  std::sort(
      results.begin(),
      results.end(),
      [](const MoveResult& first, const MoveResult& second) {
        return GlobalObjectiveValue::unsafeCompare(
                   first.getValue(), second.getValue()) < 0;
      });

  for (auto& result : results) {
    if (!result.isBetter(precision)) {
      // no need to check further, as results are sorted by objective value
      break;
    }

    bool allMovable = true;
    for (auto& move : result.getMoveSet()) {
      if (problem.fixed_objects.contains(move.getObject())) {
        allMovable = false;
        break;
      }
    }

    if (allMovable) {
      auto moveSummary =
          MovesSummaryHelper::makeMovesSummary(problem, result, stats);
      XLOG_EVERY_MS(DBG1, 1000)
          << "Move " << result.toString(problem.getUniverse())
          << " not applied.";
    }
  }

  return MoveResult::makeEmpty();
}

std::string MoveType::getMoveDescription(
    const interface::MovesSummary& moveSummary) {
  double maxObjectiveChange = 0;
  const std::string* maxObjectiveName = nullptr;
  for (auto& it : *moveSummary.objectives()) {
    if (*it.second.change() > maxObjectiveChange) {
      maxObjectiveChange = *it.second.change();
      maxObjectiveName = &it.first;
    }
  }

  if (!maxObjectiveName) {
    throw std::runtime_error(
        "No objective changes found. Is the move valid and moveStatsSpec set correctly?");
  }

  return fmt::format(
      "ReBalancer decided to perform a move, to mainly improve {} objective by {:.4g}.",
      *maxObjectiveName,
      maxObjectiveChange);
  // TODO add support for move tags. Code from Java - https://fburl.com/7ibmsqvw
}

ReferenceList<const std::vector<entities::ContainerId>>
MoveType::getDestinationsToExplore(
    const interface::DestinationsToExploreOptions& destinationsToExplore,
    entities::ContainerId hotContainerId,
    entities::ObjectId hotObjectId,
    Problem& problem) {
  auto exploreOption = destinationsToExplore.getType();
  switch (exploreOption) {
    case interface::DestinationsToExploreOptions::Type::
        moveToCurrentScopeItem: {
      return problem.getDestinationsGenerator().getAcceptingDestinations(
          destinationsToExplore.get_moveToCurrentScopeItem(), hotContainerId);
    }

    case interface::DestinationsToExploreOptions::Type::moveToScopeItems: {
      return problem.getDestinationsGenerator().getAcceptingDestinations(
          destinationsToExplore.get_moveToScopeItems(),
          hotContainerId,
          hotObjectId);
    }

    case interface::DestinationsToExploreOptions::Type::
        destinationToExploreName: {
      const auto& universe = problem.getUniverse();
      return getDestinationsToExplore(
          universe.getDestinationsToExploreOptions(
              destinationsToExplore.get_destinationToExploreName()),
          hotContainerId,
          hotObjectId,
          problem);
    }

    case interface::DestinationsToExploreOptions::Type::__EMPTY__: {
      throw std::runtime_error(
          "DestinationToExploreOptions is empty; you need to specify one of the options");
    }
  }
}

std::vector<ObjectBundle> MoveType::getObjectBundlesToExplore(
    const interface::ObjectsToExploreOptions& objectsToExploreOptions,
    entities::ContainerId srcContainer,
    const folly::F14FastMap<entities::GroupId, int>& groupBundleSizeOverrides,
    Problem& problem,
    bool createGreedyHeterogenousBundles) {
  const int bundleSize = getBundleSize(objectsToExploreOptions);
  if (bundleSize < 1) {
    throw std::runtime_error(
        fmt::format(
            "getObjectBundlesToExplore called with bundleSize: {}",
            bundleSize));
  }
  auto exploreOptions = objectsToExploreOptions.getType();
  switch (exploreOptions) {
    case interface::ObjectsToExploreOptions::Type::objectsFromGroupsSpec: {
      const std::string& partition =
          *objectsToExploreOptions.get_objectsFromGroupsSpec()
               .groupList()
               ->partitionName();
      return problem.getObjectsGenerator().getObjectBundlesToExplore(
          partition,
          srcContainer,
          bundleSize,
          groupBundleSizeOverrides,
          problem.assignment,
          problem.getEquivalenceSets(),
          createGreedyHeterogenousBundles);
    }
    case interface::ObjectsToExploreOptions::Type::__EMPTY__: {
      throw std::runtime_error(
          "ObjectsToExploreOptions is empty; you need to specify one of the options");
    }
  }
}

ReferenceList<const std::vector<entities::ObjectId>>
MoveType::getObjectsToExplore(
    const interface::ObjectsToExploreOptions& objectsToExplore,
    entities::ContainerId hotContainerId,
    Problem& problem) {
  auto exploreOptions = objectsToExplore.getType();
  switch (exploreOptions) {
    case interface::ObjectsToExploreOptions::Type::objectsFromGroupsSpec:
      return problem.getObjectsGenerator().getObjectsToExplore(
          *objectsToExplore.get_objectsFromGroupsSpec()
               .groupList()
               ->partitionName(),
          hotContainerId,
          problem.assignment,
          problem.getEquivalenceSets());
    case interface::ObjectsToExploreOptions::Type::__EMPTY__:
      throw std::runtime_error(
          "ObjectsToExploreOptions is empty; you need to specify one of the options");
  }
}

int MoveType::getSampleSize(
    const interface::SampleSize& sampleSize,
    const std::string& objectName) {
  return folly::get_default(
      *sampleSize.objectToSampleSize(),
      objectName,
      *sampleSize.defaultSampleSize());
}

} // namespace facebook::rebalancer
