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

#include "algopt/rebalancer/solver/expressions/ObjectVector.h"

#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/Enumerate.h>

#include <sstream>

namespace {
constexpr std::string_view type = "ObjectVector";
}

namespace facebook::rebalancer {

using entities::ContainerId;
using entities::EquivalenceSetId;
using entities::ObjectId;

ObjectVector::ObjectVector(
    entities::ObjectValues objectValues,
    const entities::Universe& universe)
    : Expression(universe), objectValues_(std::move(objectValues)) {
  defaultValue_ = objectValues_.defaultValue();
  totalObjects_ = objectValues_.totalObjectCount();

  allIntegerValues_ = true;
  const auto& precision = getPrecision();
  const auto numNonDefaultValues = objectValues_.nonDefaultCount();
  // if at least one object takes default value and that is not an integer, then
  // allIntegerValues_ is false
  if (numNonDefaultValues < totalObjects_ &&
      !precision.isInteger(defaultValue_)) {
    allIntegerValues_ = false;
  }

  if (totalObjects_ < numNonDefaultValues) {
    throw std::runtime_error(
        fmt::format(
            "ObjectVector: size of objectValues {} must not exceed totalObjects {}",
            numNonDefaultValues,
            totalObjects_));
  }

  lowerBound_ = 0;
  upperBound_ = 0;
  hasNegativeValues_ =
      (numNonDefaultValues < totalObjects_) && defaultValue_ < 0;
  objectValues_.forEachNonDefault([&](entities::ObjectId, double value) {
    if (!precision.isInteger(value)) {
      allIntegerValues_ = false;
    }
    hasNegativeValues_ = hasNegativeValues_ || (value < 0);
    if (value < 0) {
      lowerBound_ += value;
    } else if (value > 0) {
      upperBound_ += value;
    }
  });

  if (defaultValue_ < 0) {
    lowerBound_ += (totalObjects_ - numNonDefaultValues) * defaultValue_;
  } else if (defaultValue_ > 0) {
    upperBound_ += (totalObjects_ - numNonDefaultValues) * defaultValue_;
  }
}

const std::string_view& ObjectVector::getType() const {
  return type;
}

bool ObjectVector::has_negative_values() const {
  return hasNegativeValues_;
}

void ObjectVector::updateEquivalenceSets(EquivalenceSets& /* unused */) const {
  // no-op, caller chooses appropriate object vector and calls
  // passiveUpdateEquivalenceSets
}

void ObjectVector::passiveUpdateEquivalenceSets(
    EquivalenceSets& equivalenceSets) const {
  equivalenceSets.mappingMerge(getId(), objectValues_);
}

PackerMap<EquivalenceSetId, int> ObjectVector::getEquivSetCount(
    const Problem& problem,
    const PackerSet<entities::ContainerId>& containers) const {
  PackerMap<EquivalenceSetId, int> equivSetCount;
  if (defaultValue_ == 0) {
    objectValues_.forEachNonDefault([&](ObjectId object, double /*value*/) {
      const EquivalenceSetId equivSet = problem.getEquivalenceSets().at(object);
      equivSetCount[equivSet] +=
          containers.contains(problem.initial_assignment.getContainer(object));
    });
  } else {
    // When the default value is non-zero, any objects not explicitly listed
    // in objectValues_ may have a contributution.
    for (auto containerIdx : containers) {
      for (auto object : problem.initial_assignment.getObjects(containerIdx)) {
        auto equivSet = problem.getEquivalenceSets().at(object);
        equivSetCount[equivSet] += 1;
      }
    }
  }
  return equivSetCount;
}

bool ObjectVector::inner_is_integer(Context&) {
  return allIntegerValues_;
}

Bounds ObjectVector::innerLowerAndUpperBounds(
    Context& /* unused */,
    const BoundConstraints& /* unused */) const {
  return {.lower_bound = lowerBound_, .upper_bound = upperBound_};
}

double ObjectVector::getObjectValue(ObjectId objectId) const {
  return objectValues_.getObjectValue(objectId);
}

bool ObjectVector::isZeroDefault() const {
  return defaultValue_ == 0.0;
}

bool ObjectVector::references(ObjectId object) const {
  return !isZeroDefault() || objectValues_.getObjectValue(object) != 0.0;
}

size_t ObjectVector::nonDefaultCount() const {
  return objectValues_.nonDefaultCount();
}

size_t ObjectVector::totalObjectCount() const {
  return totalObjects_;
}

std::shared_ptr<const entities::Set<entities::ObjectId>>
ObjectVector::nonDefaultObjectIds() const {
  return nonDefaultObjectIds_.getSavedOrCompute([&]() {
    auto nonDefaultObjectIds =
        std::make_shared<entities::Set<entities::ObjectId>>();
    objectValues_.forEachNonDefault(
        [&](entities::ObjectId objectId, double /*value*/) {
          nonDefaultObjectIds->insert(objectId);
        });
    return nonDefaultObjectIds;
  });
}

void ObjectVector::forEachNonDefaultEntry(
    folly::FunctionRef<void(entities::ObjectId, double)> fn) const {
  objectValues_.forEachNonDefault(fn);
}

PackerMap<entities::EquivalenceSetId, double> ObjectVector::computeEquivSetMap(
    const EquivalenceSets& equivalenceSets) const {
  PackerMap<entities::EquivalenceSetId, double> equivSetMap;
  objectValues_.forEachNonDefault([&](entities::ObjectId object, double value) {
    const entities::EquivalenceSetId equivSet = equivalenceSets.at(object);
    auto [it, inserted] = equivSetMap.try_emplace(equivSet, value);
    if (!inserted && it->second != value) {
      throw std::runtime_error(
          fmt::format(
              "The value {} of object {} is not consistent with value {} of its equiv set {}",
              value,
              object,
              it->second,
              equivSet));
    }
  });
  if (defaultValue_ != 0.0) {
    for (auto equivSet : equivalenceSets.ids()) {
      if (!equivSetMap.contains(equivSet)) {
        equivSetMap[equivSet] = defaultValue_;
      }
    }
  }
  return equivSetMap;
}

std::string ObjectVector::innerDigest(size_t maxChildren) const {
  std::stringstream ss;
  size_t idx = 0;
  objectValues_.forEachNonDefault(
      [&](entities::ObjectId objectId, double value) {
        if (idx == maxChildren) {
          return;
        }
        if (idx > 0) {
          ss << ", ";
        }
        ss << universe_->getEntityName(objectId) << ":" << value;
        ++idx;
      });
  if (idx == maxChildren && objectValues_.nonDefaultCount() > maxChildren) {
    ss << "... " << objectValues_.nonDefaultCount() - maxChildren << " more";
  }
  return ss.str();
}

ExpressionProperties ObjectVector::getProperties() const {
  ExpressionProperties properties;
  properties.properties()->emplace(
      "default_value", PropertiesHelper::makeDoubleValue(defaultValue_));
  properties.properties()->emplace(
      "object_values",
      PropertiesHelper::makeObjectIdDoubleMapValue(objectValues_));
  return properties;
}

bool ObjectVector::hasNoLpIntent() const {
  return true;
}

bool ObjectVector::hasValue() const {
  return false;
}

} // namespace facebook::rebalancer
