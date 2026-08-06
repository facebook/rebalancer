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

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"

#include <folly/container/irange.h>
#include <folly/Conv.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/executors/ThreadPoolExecutor.h>

#include <stdexcept>
#include <string>

namespace facebook::rebalancer::interface {

class AsyncConfig {
 public:
  explicit AsyncConfig(std::shared_ptr<folly::ThreadPoolExecutor> executor);
  ~AsyncConfig();

  AsyncConfig(const AsyncConfig&) = delete;
  AsyncConfig& operator=(const AsyncConfig&) = delete;
  AsyncConfig(AsyncConfig&&) = delete;
  AsyncConfig& operator=(AsyncConfig&&) = delete;

  void add(folly::coro::Task<void>&& task);
  void waitForAllPending();

  const std::shared_ptr<folly::ThreadPoolExecutor>& executor() const {
    return executor_;
  }

 private:
  std::shared_ptr<folly::ThreadPoolExecutor> executor_{nullptr};
  folly::coro::AsyncScope asyncScope_;
  bool joined_{false};
};

class UniverseProblemBuilder {
 public:
  explicit UniverseProblemBuilder(std::unique_ptr<AsyncConfig> asyncConfig)
      : asyncConfig_(std::move(asyncConfig)) {}

  ~UniverseProblemBuilder() = default;

  void setObjectName(const std::string& objectName);
  void setContainerName(const std::string& containerName);

  template <typename ContainerToObjects>
  void setAssignment(const ContainerToObjects& containerToObjects)
    requires IsIterableOverPairs<
        ContainerToObjects,
        std::string,
        std::vector<std::string>>;

  template <typename ScopeItemToObjectToValue>
  void addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      ScopeItemToObjectToValue scopeItemToObjectToValue,
      double defaultValue)
    requires IsMapOfMap<
        ScopeItemToObjectToValue,
        std::string,
        std::string,
        double>;

  template <typename ScopeItemToGroupToValue>
  void addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      const std::string& partitionName,
      ScopeItemToGroupToValue scopeItemToGroupToValue,
      double defaultValue)
    requires IsMapOfMap<
        ScopeItemToGroupToValue,
        std::string,
        std::string,
        double>;

  template <typename ObjectToGroup>
  void addPartition(
      const std::string& partitionName,
      ObjectToGroup objectToGroup)
    requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>;

  template <typename GroupToObjects>
  void addPartition(
      const std::string& partitionName,
      GroupToObjects groupToObjects)
    requires IsIterableOverPairs<
        GroupToObjects,
        std::string,
        std::vector<std::string>>;

  template <typename ObjectToValue>
  void addObjectDimension(
      const std::string& dimensionName,
      ObjectToValue objectToValue,
      double defaultValue = 0,
      std::optional<ObjectToValue> scaleByUsageMap = std::nullopt)
    requires IsIterableOverPairs<ObjectToValue, std::string, double>;

  template <typename ObjectToValues>
  void addObjectDimension(
      const std::string& dimensionName,
      ObjectToValues objectToValues,
      double defaultValue = 0)
    requires IsIterableOverPairs<
        ObjectToValues,
        std::string,
        std::vector<double>>;

  void addObjectPartitionRoutingDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      double defaultValue = 0,
      const std::unordered_map<std::string, double>& groupToStaticValue = {},
      double defaultStaticValue = 0);

  template <typename ContainerToValue>
  void addContainerDimension(
      const std::string& dimensionName,
      const ContainerToValue& containerToValue,
      double defaultValue = 1.0)
    requires IsIterableOverPairs<ContainerToValue, std::string, double>;

  template <typename ScopeItemToValue>
  void addScopeDimension(
      const std::string& dimensionName,
      const std::string& scopeName,
      const ScopeItemToValue& scopeItemToValue,
      double defaultValue = 1.0)
    requires IsIterableOverPairs<ScopeItemToValue, std::string, double>;

  template <typename ContainerToScopeItem>
  void addScope(
      const std::string& scopeName,
      const ContainerToScopeItem& containerToScopeItem)
    requires IsIterableOverPairs<ContainerToScopeItem, std::string, std::string>
  ;

  template <typename ScopeItemToContainers>
  void addScope(
      const std::string& scopeName,
      const ScopeItemToContainers& scopeItemToContainers)
    requires IsIterableOverPairs<
        ScopeItemToContainers,
        std::string,
        std::vector<std::string>>;

  void addSimilarContainers(
      const std::vector<std::vector<std::string>>& similarContainerClasses);

  void addRoutingConfig(
      const std::string& configName,
      const std::string& scopeName,
      const std::string& partitionName,
      const std::unordered_map<std::string, interface::GroupRoutingRings>&
          groupToRoutingRings,
      const std::unordered_map<
          std::string,
          std::unordered_map<std::string, double>>& originToDestinationLatency,
      const std::optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>&
          defaultOriginToDestinationScopeItemSets);

  template <typename Spec>
  void addGoal(Spec spec, double weight, int tuplePos)
    requires FieldTypeExistsInThriftStructOrUnion<interface::GoalSpecs, Spec>;

  template <typename Spec>
  void addConstraint(
      Spec spec,
      std::optional<ConstraintPolicy> policy,
      std::optional<double> invalidCost,
      std::optional<double> invalidState,
      std::optional<int> tuplePosIfBroken)
    requires(
        FieldTypeExistsInThriftStructOrUnion<interface::ConstraintSpecs, Spec>);

  void enableRestrictMovingObjectOnlyOnce();
  void enableStableAsMuchAsPossible();
  void setGroupBackedDynamicDimensions(bool enable);
  void setFeasibilityTolerance(double feasibilityTolerance);

  void setConstraintPolicy(ConstraintPolicy policy);
  void setDefaultConstraintParams(const ConstraintParams& constraintParams);
  void setPrecision(
      algopt::common::thrift::PrecisionTolerances& precisionTolerances);

  void overrideContainerHotnessRanking(
      const std::vector<std::string>& descendingHotnessContainers);

  void setObjectOrderingDimension(const std::string& dimension);

  void addDestinationsToExploreOptions(
      const std::string& name,
      DestinationsToExploreOptions destinationsToExploreOptions);

  void printSummary() const;
  std::string getSummary() const;

  std::shared_ptr<const entities::Universe> build();

 private:
  template <typename Spec>
  void setEmptyOrUnsetDimensionField(Spec& spec)
    requires(
        std::same_as<decltype(spec), LogicalAndSpec&> ||
        std::same_as<decltype(spec), LogicalOrSpec&>);
  void setEmptyOrUnsetDimensionField(MultipleOrCapacitySpec& spec);
  void setEmptyOrUnsetDimensionField(GenericSpec& spec);

  entities::ObjectIdToDoubleMap getScaledObjectDimension(
      const entities::ObjectIdToDoubleMap& objectValues,
      double defaultValue,
      const entities::Map<entities::ContainerId, double>& containerUsage) const;

  template <class T>
  void addScopeDimensionImpl(
      const std::string& dimensionName,
      entities::ScopeId scopeId,
      const T& scopeItemToValue,
      double defaultValue);

  template <class T>
  void addContainerDimensionImpl(
      const std::string& dimensionName,
      const T& containerToValue,
      double defaultValue);

  template <class T>
  void addScopeDimensionImpl(
      const std::string& dimensionName,
      const std::string& scopeName,
      const T& scopeItemToValue,
      double defaultValue);

  std::string getObjectCountDimensionName() const;

  template <typename Spec>
  GoalSpecs getGoalSpecsUnion(Spec spec);

  void addGoal(std::string name, GoalSpecs spec, double weight, int tuplePos);

  template <typename Spec>
  ConstraintSpecs getConstraintSpecsUnion(Spec&& spec);

  void addConstraint(
      std::string name,
      ConstraintSpecs spec,
      std::optional<ConstraintPolicy> policy,
      std::optional<double> invalidCost,
      std::optional<double> invalidState,
      std::optional<int> tuplePosIfBroken);

  static interface::CapacitySpec fromThrottlingToCapacitySpec(
      const ThrottlingSpec& spec);

  interface::ColocateGroupsSpec toColocateGroupsSpec(PairAffinitiesSpec spec);

  template <typename ScopeItemToObjectToValue>
  folly::coro::Task<void> addDynamicObjectDimensionImpl(
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      ScopeItemToObjectToValue scopeItemToObjectToValue,
      double defaultValue);

  template <typename ScopeItemToGroupToValue>
  folly::coro::Task<void> addDynamicObjectDimensionImpl(
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::PartitionId partitionId,
      ScopeItemToGroupToValue scopeItemToGroupToValue,
      double defaultValue);

  template <typename ObjectToGroup>
  folly::coro::Task<void> addPartitionImpl(
      entities::PartitionId partitionId,
      ObjectToGroup objectToGroup)
    requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>;

  template <typename GroupToObjects>
  folly::coro::Task<void> addPartitionImpl(
      entities::PartitionId partitionId,
      GroupToObjects groupToObjects)
    requires IsIterableOverPairs<
        GroupToObjects,
        std::string,
        std::vector<std::string>>;

  template <typename ObjectToValue>
  folly::coro::Task<void> addObjectDimensionImpl(
      entities::DimensionId dimensionId,
      ObjectToValue objectToValue,
      double defaultValue,
      std::optional<ObjectToValue> scaleByUsageMap)
    requires IsIterableOverPairs<ObjectToValue, std::string, double>;

  template <typename ObjectToValues>
  folly::coro::Task<void> addObjectDimensionImpl(
      entities::DimensionId dimensionId,
      ObjectToValues objectToValues,
      double defaultValue)
    requires IsIterableOverPairs<
        ObjectToValues,
        std::string,
        std::vector<double>>;

  void maybeExecuteAsync(folly::coro::Task<void>&& task) {
    if (asyncConfig_) {
      asyncConfig_->add(std::move(task));
    } else {
      folly::coro::blockingWait(std::move(task));
    }
  }

 private:
  entities::AsyncUniverseBuilder universe_;
  std::atomic<int> goalCount_ = 0;
  std::atomic<int> constraintCount_ = 0;
  ConstraintPolicy globalConstraintPolicy_ = ConstraintPolicy::DEFAULT;
  ConstraintParams constraintParams_;
  constexpr static std::string_view kDimensionFieldName = "dimension";
  std::unique_ptr<AsyncConfig> asyncConfig_{nullptr};
  bool useGroupBackedDynamicDimensions_{false};
};

// implementations
template <typename ContainerToValue>
void UniverseProblemBuilder::addContainerDimension(
    const std::string& dimensionName,
    const ContainerToValue& containerToValue,
    double defaultValue)
  requires IsIterableOverPairs<ContainerToValue, std::string, double>
{
  addContainerDimensionImpl(dimensionName, containerToValue, defaultValue);
}

template <typename ScopeItemToObjectToValue>
void UniverseProblemBuilder::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    ScopeItemToObjectToValue scopeItemToObjectToValue,
    double defaultValue)
  requires IsMapOfMap<
      ScopeItemToObjectToValue,
      std::string,
      std::string,
      double>
{
  const auto dimensionId = universe_.makeObjectDimensionId(dimensionName);
  const auto scopeId = universe_.getScopeId(scopeName);
  maybeExecuteAsync(addDynamicObjectDimensionImpl(
      dimensionId, scopeId, std::move(scopeItemToObjectToValue), defaultValue));
}

template <typename ScopeItemToObjectToValue>
folly::coro::Task<void> UniverseProblemBuilder::addDynamicObjectDimensionImpl(
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    ScopeItemToObjectToValue scopeItemToObjectToValue,
    double defaultValue) {
  const auto objects = universe_.getObjects();
  const auto scope = co_await universe_.getScope(scopeId);
  const auto totalObjects = objects->numObjects;

  entities::Map<
      entities::ScopeItemId,
      std::shared_ptr<const entities::ObjectIdToDoubleMap>>
      scopeItemIdToObjectIdToValue;
  scopeItemIdToObjectIdToValue.reserve(
      folly::to<decltype(scopeItemIdToObjectIdToValue)::size_type>(
          scopeItemToObjectToValue.size()));
  co_await CoroUtils::runEachFuncAndUpdate(
      scopeItemToObjectToValue.begin(),
      scopeItemToObjectToValue.end(),
      [&](const auto& it) {
        const auto& [scopeItemName, objectToValue] = *it;
        entities::ObjectIdToDoubleMap objectIdToValue(
            totalObjects,
            defaultValue,
            /*expectedNonDefaultSize=*/objectToValue.size());
        for (auto& [objectName, objectValue] : objectToValue) {
          if (objectValue == defaultValue) {
            continue;
          }
          const auto objectId = objects->getId(objectName);
          objectIdToValue.emplace(objectId, objectValue);
        }
        return objectIdToValue;
      },
      [&](auto&& objectIdToValue, const auto& it) {
        const auto& [scopeItemName, _] = *it;
        const auto scopeItemId = scope->getScopeItemId(scopeItemName);
        scopeItemIdToObjectIdToValue.emplace(
            scopeItemId,
            std::make_shared<const entities::ObjectIdToDoubleMap>(
                std::move(objectIdToValue)));
      });

  co_return co_await universe_.addObjectDimension(
      dimensionId,
      entities::ObjectDimensionData{
          .dimension = std::make_unique<const entities::ObjectDimension>(
              scopeId,
              std::move(scopeItemIdToObjectIdToValue),
              defaultValue,
              totalObjects)});
}

template <typename ScopeItemToGroupToValue>
void UniverseProblemBuilder::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    const std::string& partitionName,
    ScopeItemToGroupToValue scopeItemToGroupToValue,
    double defaultValue)
  requires IsMapOfMap<ScopeItemToGroupToValue, std::string, std::string, double>
{
  const auto dimensionId = universe_.makeObjectDimensionId(dimensionName);
  const auto scopeId = universe_.getScopeId(scopeName);
  const auto partitionId = universe_.getPartitionId(partitionName);
  maybeExecuteAsync(addDynamicObjectDimensionImpl(
      dimensionId,
      scopeId,
      partitionId,
      std::move(scopeItemToGroupToValue),
      defaultValue));
}

template <typename ScopeItemToGroupToValue>
folly::coro::Task<void> UniverseProblemBuilder::addDynamicObjectDimensionImpl(
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::PartitionId partitionId,
    ScopeItemToGroupToValue scopeItemToGroupToValue,
    double defaultValue) {
  const auto [partitionData, scope] = co_await folly::coro::collectAll(
      universe_.getPartition(partitionId), universe_.getScope(scopeId));

  if (!partitionData->partition->isDisjoint()) {
    throw std::runtime_error(
        "Group based dynamic dimension is only supported for disjoint partition.");
  }

  const auto totalObjects = universe_.getObjects()->numObjects;

  if (useGroupBackedDynamicDimensions_) {
    entities::Map<
        entities::ScopeItemId,
        std::shared_ptr<const entities::Map<entities::GroupId, double>>>
        scopeItemIdToGroupIdToValue;
    scopeItemIdToGroupIdToValue.reserve(
        static_cast<decltype(scopeItemIdToGroupIdToValue)::size_type>(
            scopeItemToGroupToValue.size()));
    for (const auto& [scopeItemName, groupToValue] : scopeItemToGroupToValue) {
      entities::Map<entities::GroupId, double> groupIdToValue;
      groupIdToValue.reserve(
          static_cast<decltype(groupIdToValue)::size_type>(
              groupToValue.size()));
      for (const auto& [groupName, value] : groupToValue) {
        // Storage ignores default-valued groups for non-default iteration, but
        // filtering here keeps the compact representation sparse before it
        // reaches the entity layer.
        if (value == defaultValue) {
          continue;
        }
        groupIdToValue.emplace(partitionData->getGroupId(groupName), value);
      }
      scopeItemIdToGroupIdToValue.emplace(
          scope->getScopeItemId(scopeItemName),
          std::make_shared<const entities::Map<entities::GroupId, double>>(
              std::move(groupIdToValue)));
    }

    co_return co_await universe_.addObjectDimension(
        dimensionId,
        entities::ObjectDimensionData{
            .dimension = std::make_unique<const entities::ObjectDimension>(
                scopeId,
                partitionData->partition,
                std::move(scopeItemIdToGroupIdToValue),
                defaultValue,
                totalObjects,
                partitionId)});
  }

  entities::Map<
      entities::ScopeItemId,
      std::shared_ptr<const entities::ObjectIdToDoubleMap>>
      scopeItemIdToObjectIdToValue;
  scopeItemIdToObjectIdToValue.reserve(scopeItemToGroupToValue.size());
  co_await CoroUtils::runEachFuncAndUpdate(
      scopeItemToGroupToValue.begin(),
      scopeItemToGroupToValue.end(),
      [&](const auto& it) {
        const auto& [scopeItemName, groupToValue] = *it;
        std::size_t expectedNonDefault = 0;
        for (const auto& [groupName, value] : groupToValue) {
          if (value == defaultValue) {
            continue;
          }
          expectedNonDefault +=
              partitionData->partition
                  ->getObjectIds(partitionData->getGroupId(groupName))
                  .size();
        }

        entities::ObjectIdToDoubleMap objectIdToValue(
            totalObjects, defaultValue, expectedNonDefault);
        for (const auto& [groupName, value] : groupToValue) {
          if (value == defaultValue) {
            continue;
          }
          const auto groupId = partitionData->getGroupId(groupName);
          const auto& objectIds =
              partitionData->partition->getObjectIds(groupId);
          for (const auto objectId : objectIds) {
            objectIdToValue.emplace(objectId, value);
          }
        }
        return objectIdToValue;
      },
      [&](auto&& objectIdToValue, const auto& it) {
        const auto& [scopeItemName, _] = *it;
        const auto scopeItemId = scope->getScopeItemId(scopeItemName);
        scopeItemIdToObjectIdToValue.emplace(
            scopeItemId,
            std::make_shared<const entities::ObjectIdToDoubleMap>(
                std::move(objectIdToValue)));
      });

  co_return co_await universe_.addObjectDimension(
      dimensionId,
      entities::ObjectDimensionData{
          .dimension = std::make_unique<const entities::ObjectDimension>(
              scopeId,
              std::move(scopeItemIdToObjectIdToValue),
              defaultValue,
              totalObjects)});
}

template <typename ObjectToGroup>
void UniverseProblemBuilder::addPartition(
    const std::string& partitionName,
    ObjectToGroup objectToGroup)
  requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>
{
  const auto partitionId = universe_.makePartitionId(partitionName);
  maybeExecuteAsync(addPartitionImpl(partitionId, std::move(objectToGroup)));
}

template <typename ObjectToGroup>
folly::coro::Task<void> UniverseProblemBuilder::addPartitionImpl(
    entities::PartitionId partitionId,
    ObjectToGroup objectToGroup)
  requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>
{
  co_return co_await universe_.addPartition(
      partitionId, std::move(objectToGroup));
}

template <typename GroupToObjects>
void UniverseProblemBuilder::addPartition(
    const std::string& partitionName,
    GroupToObjects groupToObjects)
  requires IsIterableOverPairs<
      GroupToObjects,
      std::string,
      std::vector<std::string>>
{
  const auto partitionId = universe_.makePartitionId(partitionName);
  maybeExecuteAsync(addPartitionImpl(partitionId, std::move(groupToObjects)));
}

template <typename GroupToObjects>
folly::coro::Task<void> UniverseProblemBuilder::addPartitionImpl(
    entities::PartitionId partitionId,
    GroupToObjects groupToObjects)
  requires IsIterableOverPairs<
      GroupToObjects,
      std::string,
      std::vector<std::string>>
{
  co_return co_await universe_.addPartition(
      partitionId, std::move(groupToObjects));
}

template <typename ScopeItemToValue>
void UniverseProblemBuilder::addScopeDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    const ScopeItemToValue& scopeItemToValue,
    double defaultValue)
  requires IsIterableOverPairs<ScopeItemToValue, std::string, double>
{
  addScopeDimensionImpl(
      dimensionName, scopeName, scopeItemToValue, defaultValue);
}

template <typename ContainerToScopeItem>
void UniverseProblemBuilder::addScope(
    const std::string& scopeName,
    const ContainerToScopeItem& containerToScopeItem)
  requires IsIterableOverPairs<ContainerToScopeItem, std::string, std::string>
{
  const auto scopeId = universe_.makeScopeId(scopeName);
  folly::coro::blockingWait(universe_.addScope(scopeId, containerToScopeItem));
}

template <typename ScopeItemToContainers>
void UniverseProblemBuilder::addScope(
    const std::string& scopeName,
    const ScopeItemToContainers& scopeItemToContainers)
  requires IsIterableOverPairs<
      ScopeItemToContainers,
      std::string,
      std::vector<std::string>>
{
  const auto scopeId = universe_.makeScopeId(scopeName);
  folly::coro::blockingWait(universe_.addScope(scopeId, scopeItemToContainers));
}

template <typename ContainerToObjects>
void UniverseProblemBuilder::setAssignment(
    const ContainerToObjects& containerToObjects)
  requires IsIterableOverPairs<
      ContainerToObjects,
      std::string,
      std::vector<std::string>>
{
  universe_.setInitialAssignment(containerToObjects);
}

template <typename ObjectToValue>
void UniverseProblemBuilder::addObjectDimension(
    const std::string& dimensionName,
    ObjectToValue objectToValue,
    double defaultValue,
    std::optional<ObjectToValue> scaleByUsageMap)
  requires IsIterableOverPairs<ObjectToValue, std::string, double>
{
  const auto dimensionId = universe_.makeObjectDimensionId(dimensionName);
  maybeExecuteAsync(addObjectDimensionImpl(
      dimensionId,
      std::move(objectToValue),
      defaultValue,
      std::move(scaleByUsageMap)));
}

template <typename ObjectToValue>
folly::coro::Task<void> UniverseProblemBuilder::addObjectDimensionImpl(
    entities::DimensionId dimensionId,
    ObjectToValue objectToValue,
    double defaultValue,
    std::optional<ObjectToValue> scaleByUsageMap)
  requires IsIterableOverPairs<ObjectToValue, std::string, double>
{
  const auto objects = universe_.getObjects();
  const auto totalObjects = objects->numObjects;

  entities::ObjectIdToDoubleMap objectValues(
      totalObjects,
      defaultValue,
      /*expectedNonDefaultSize=*/objectToValue.size());
  for (auto& [objectName, objectValue] : objectToValue) {
    auto objectId = objects->getId(objectName);
    objectValues.emplace(objectId, objectValue);
  }
  if (scaleByUsageMap) {
    const auto containers = universe_.getContainers();
    entities::Map<entities::ContainerId, double> containerUsage;
    for (auto& [containerName, usage] : *scaleByUsageMap) {
      containerUsage.emplace(containers->getId(containerName), usage);
    }
    objectValues =
        getScaledObjectDimension(objectValues, defaultValue, containerUsage);
  }

  co_return co_await universe_.addObjectDimension(
      dimensionId,
      entities::ObjectDimensionData{
          .dimension = std::make_unique<const entities::ObjectDimension>(
              std::move(objectValues))});
}

template <typename ObjectToValues>
void UniverseProblemBuilder::addObjectDimension(
    const std::string& dimensionName,
    ObjectToValues objectToValues,
    double defaultValue)
  requires IsIterableOverPairs<ObjectToValues, std::string, std::vector<double>>
{
  const auto dimensionId = universe_.makeObjectDimensionId(dimensionName);
  maybeExecuteAsync(addObjectDimensionImpl(
      dimensionId, std::move(objectToValues), defaultValue));
}

template <typename ObjectToValues>
folly::coro::Task<void> UniverseProblemBuilder::addObjectDimensionImpl(
    entities::DimensionId dimensionId,
    ObjectToValues objectToValues,
    double defaultValue)
  requires IsIterableOverPairs<ObjectToValues, std::string, std::vector<double>>
{
  const auto& objects = universe_.getObjects();
  const auto totalObjects = objects->numObjects;
  if (objectToValues.empty()) {
    co_return co_await universe_.addObjectDimension(
        dimensionId,
        entities::ObjectDimensionData{
            .dimension = std::make_unique<const entities::ObjectDimension>(
                entities::ObjectIdToDoubleMap(
                    totalObjects,
                    defaultValue,
                    /*expectedNonDefaultSize=*/0))});
  }

  size_t vectorSize = objectToValues.begin()->second.size();
  std::vector<entities::ObjectIdToDoubleMap> objectValues(
      vectorSize,
      entities::ObjectIdToDoubleMap(
          totalObjects,
          defaultValue,
          /*expectedNonDefaultSize=*/objectToValues.size()));
  for (auto& [objectName, objectVector] : objectToValues) {
    // check that all vectors have the same size
    if (objectVector.size() != vectorSize) {
      throw std::runtime_error(
          fmt::format(
              "All vectors in objectToValues must have the same size, but found that vector w.r.t. object '{}' has size {} while vector w.r.t. '{}' has size {}",
              objectName,
              objectVector.size(),
              objectToValues.begin()->first,
              vectorSize));
    }

    const auto objectId = objects->getId(objectName);
    for (const auto i : folly::irange(vectorSize)) {
      double objectValue = objectVector.at(i);
      if (objectValue == defaultValue) {
        continue;
      }
      objectValues.at(i).emplace(objectId, objectValue);
    }
  }

  co_return co_await universe_.addObjectDimension(
      dimensionId,
      entities::ObjectDimensionData{
          .dimension = std::make_unique<const entities::ObjectDimension>(
              std::move(objectValues))});
}

template <typename Spec>
void UniverseProblemBuilder::addGoal(Spec spec, double weight, int tuplePos)
  requires FieldTypeExistsInThriftStructOrUnion<interface::GoalSpecs, Spec>
{
  auto specName = folly::copy(*spec.name());
  addGoal(
      std::move(specName),
      getGoalSpecsUnion(std::move(spec)),
      weight,
      tuplePos);
}

template <typename Spec>
void UniverseProblemBuilder::addConstraint(
    Spec spec,
    std::optional<ConstraintPolicy> policy,
    std::optional<double> invalidCost,
    std::optional<double> invalidState,
    std::optional<int> tuplePosIfBroken)
  requires(
      FieldTypeExistsInThriftStructOrUnion<interface::ConstraintSpecs, Spec>)
{
  auto specName = folly::copy(*spec.name());
  addConstraint(
      std::move(specName),
      getConstraintSpecsUnion(std::move(spec)),
      policy,
      invalidCost,
      invalidState,
      tuplePosIfBroken);
}

template <typename Spec>
ConstraintSpecs UniverseProblemBuilder::getConstraintSpecsUnion(Spec&& spec) {
  // special handling for ThrottlingSpec since it is converted to CapacitySpec
  if constexpr (std::same_as<decltype(spec), ThrottlingSpec&&>) {
    return algopt::utils::
        createThriftUnionByField<ConstraintSpecs, CapacitySpec>(
            fromThrottlingToCapacitySpec(std::forward<Spec>(spec)));
  } else if constexpr (std::same_as<decltype(spec), PairAffinitiesSpec&&>) {
    // special handling for PairAffinitiesSpec since it is converted to
    // ColocateGroupsSpec
    return algopt::utils::
        createThriftUnionByField<ConstraintSpecs, ColocateGroupsSpec>(
            toColocateGroupsSpec(std::forward<Spec>(spec)));
  }

  auto maybeModifySpec = [this](Spec& spec) -> void {
    if constexpr (
        std::same_as<decltype(spec), LogicalOrSpec&> ||
        std::same_as<decltype(spec), LogicalAndSpec&> ||
        std::same_as<decltype(spec), MultipleOrCapacitySpec&>) {
      setEmptyOrUnsetDimensionField(spec);
    } else {
      // for all other specs, set dimension to objectCountDimension if dimension
      // field is empty
      algopt::utils::setStringFieldToValueIfEmptyOrUnset(
          spec, kDimensionFieldName.data(), getObjectCountDimensionName());
    }
  };

  // in all other cases, set dimension to objectCountDimension if dimension
  // field is empty or unset, and create union
  maybeModifySpec(spec);
  return algopt::utils::createThriftUnionByField<ConstraintSpecs, Spec>(
      std::forward<Spec>(spec));
}

template <typename Spec>
GoalSpecs UniverseProblemBuilder::getGoalSpecsUnion(Spec spec) {
  // special handling for PairAffinitiesSpec since it is converted to
  // ColocateGroupsSpec
  if constexpr (std::same_as<decltype(spec), PairAffinitiesSpec>) {
    return algopt::utils::
        createThriftUnionByField<GoalSpecs, ColocateGroupsSpec>(
            toColocateGroupsSpec(std::move(spec)));
  }

  if constexpr (!std::same_as<decltype(spec), AssignmentAffinitiesSpec>) {
    // AssignmentAffinitiesSpec uses affinities when dimension is omitted.
    algopt::utils::setStringFieldToValueIfEmptyOrUnset(
        spec, kDimensionFieldName.data(), getObjectCountDimensionName());
  }

  return algopt::utils::createThriftUnionByField<GoalSpecs, Spec>(
      std::move(spec));
}

template <typename Spec>
void UniverseProblemBuilder::setEmptyOrUnsetDimensionField(Spec& spec)
  requires(
      std::same_as<decltype(spec), LogicalAndSpec&> ||
      std::same_as<decltype(spec), LogicalOrSpec&>)
{
  for (auto& genericSpec : *spec.genericSpecs()) {
    setEmptyOrUnsetDimensionField(genericSpec);
  }
}

template <class T>
void UniverseProblemBuilder::addScopeDimensionImpl(
    const std::string& dimensionName,
    const std::string& scopeName,
    const T& scopeItemToValue,
    double defaultValue) {
  const auto scopeId = universe_.getScopeId(scopeName);
  const auto dimensionId =
      universe_.makeScopeDimensionId(dimensionName, scopeId);

  const auto scope = folly::coro::blockingWait(universe_.getScope(scopeId));

  entities::Map<entities::ScopeItemId, double> values;
  for (auto& [scopeItemName, value] : scopeItemToValue) {
    const auto scopeItemId = scope->getScopeItemId(scopeItemName);
    values.emplace(scopeItemId, value);
  }

  folly::coro::blockingWait(universe_.addScopeDimension(
      dimensionId,
      scopeId,
      entities::ScopeDimensionData{
          .dimension = std::make_unique<const entities::ScopeDimension>(
              std::move(values), defaultValue)}));
}

template <class T>
void UniverseProblemBuilder::addContainerDimensionImpl(
    const std::string& dimensionName,
    const T& containerToValue,
    double defaultValue) {
  addScopeDimensionImpl(
      dimensionName,
      universe_.getContainerTypeName(),
      containerToValue,
      defaultValue);
}

} // namespace facebook::rebalancer::interface
