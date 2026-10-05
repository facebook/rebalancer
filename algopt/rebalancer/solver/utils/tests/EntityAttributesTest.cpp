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

#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/EntityAttributes.h"
#include "algopt/rebalancer/solver/utils/EntityAttributesStore.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"

#include "multifeed/hash/HeterogeneousAccess.h"

#include "gtest/gtest.h"
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>

#include <coroutine>
#include <memory>
#include <optional>
#include <stddef.h>
#include <string>
#include <utility>
#include <vector>
namespace facebook::rebalancer::packer::tests {
// Test setup:
// - 4 objects: obj0, obj1, obj2, obj3
// - 3 containers: unassigned, container0, container1
// - Objects belong to: oddGroup (obj1, obj3), evenGroup (obj0, obj2)
// - Containers belong to: oddScope (container1), evenScope (container0)
//
// EntityAttributes tracks at any point in time the count of allowed objects
// from a group that can be moved to a scope item. The evaluateFn enforces:
// - oddGroup objects can only move to oddScope containers
// - evenGroup objects can only move to evenScope containers
// - And returns the allowed objects that can be moved from groupId to
// scopeItemId
class AllowedScopeItemsPerGroupTest : public ExpressionTestsBase {
 protected:
  folly::coro::Task<void> setUpUniverse() {
    // Set up initial assignment
    const entities::Map<std::string, std::vector<std::string>>
        initialAssignment = {{
            {"unassigned", {"obj0", "obj1", "obj2", "obj3"}},
            {"container0", {}},
            {"container1", {}},
        }};

    setInitialAssignment(initialAssignment);

    // Create partition with oddGroup and evenGroup
    const entities::Map<std::string, std::vector<std::string>> groupToObjects =
        {{"oddGroup", {"obj1", "obj3"}}, {"evenGroup", {"obj0", "obj2"}}};
    co_await addPartition("partition", groupToObjects);

    // Create scope with oddScope and evenScope
    entities::Map<std::string, std::vector<std::string>> scopeItemToContainers;
    scopeItemToContainers["oddScope"] = {"container1"};
    scopeItemToContainers["evenScope"] = {"container0"};
    co_await addScope("scope", scopeItemToContainers);

    universe_ = buildUniverse();
  }

  std::shared_ptr<const entities::Universe> universe_;
};

CO_TEST_F(AllowedScopeItemsPerGroupTest, ReturnsCountOfUnassignedObjects) {
  co_await setUpUniverse();

  const auto obj0 = object("obj0");
  const auto obj1 = object("obj1");
  const auto obj2 = object("obj2");
  const auto obj3 = object("obj3");

  const auto unassigned = container("unassigned");
  const auto container0Id = container("container0");

  const auto oddEvenPartitionId = partitionId("partition");
  const auto oddGroup = groupId(oddEvenPartitionId, "oddGroup");
  const auto evenGroup = groupId(oddEvenPartitionId, "evenGroup");

  const auto parityScope = scopeId("scope");
  const auto oddScope = scopeItemId(parityScope, "oddScope");
  const auto evenScope = scopeItemId(parityScope, "evenScope");

  auto assignment =
      Assignment(universe_->getContainers().getInitialAssignment());

  auto evaluateFn = [&](entities::GroupId groupId,
                        entities::ScopeItemId scopeItemId) -> size_t {
    const auto& allUnassignedObjects = assignment.getObjects(unassigned);

    // oddObject not allowed in evenScope and vice-versa
    if ((groupId == oddGroup && scopeItemId == evenScope) ||
        (groupId == evenGroup && scopeItemId == oddScope)) {
      return 0;
    }

    size_t oddCount = 0, evenCount = 0;
    for (const auto& obj : allUnassignedObjects) {
      if (obj == obj1 || obj == obj3) {
        oddCount++;
      } else {
        evenCount++;
      }
    }

    return groupId == oddGroup ? oddCount : evenCount;
  };

  auto store = EntityAttributesStore{};
  store.registerAttributes(
      "test",
      std::make_shared<AllowedScopeItemsPerGroup>(
          universe_->getScope(parityScope),
          universe_->getPartition(oddEvenPartitionId),
          evaluateFn));

  // Test initial state: All 4 objects are unassigned.
  // Fetch attributes and verify counts for valid and invalid group-scope pairs.
  {
    const auto& attr = store.getAttributes<AllowedScopeItemsPerGroup>("test");

    EXPECT_EQ(2, *attr.getAllowedCount(oddGroup, oddScope));
    EXPECT_EQ(2, *attr.getAllowedCount(evenGroup, evenScope));
    EXPECT_EQ(0, *attr.getAllowedCount(evenGroup, oddScope));
    EXPECT_EQ(0, *attr.getAllowedCount(oddGroup, evenScope));
  }

  // Test after moving obj0 (even) to container0 (evenScope).
  // Fetch attributes and verify evenGroup count decrements to 1.
  {
    assignment.moveTo(obj0, container0Id);
    const auto move = Move(obj0, unassigned, container0Id);
    store.updateOnApply(move.getChangeSet());

    const auto& attr = store.getAttributes<AllowedScopeItemsPerGroup>("test");

    EXPECT_EQ(2, *attr.getAllowedCount(oddGroup, oddScope));
    EXPECT_EQ(1, *attr.getAllowedCount(evenGroup, evenScope));
  }

  // Test after moving obj2 (even) to container0 (evenScope).
  // Fetch attributes and verify evenGroup returns nullopt (no more even
  // objects).
  {
    assignment.moveTo(obj2, container0Id);
    const auto move2 = Move(obj2, unassigned, container0Id);
    store.updateOnApply(move2.getChangeSet());

    const auto& attr = store.getAttributes<AllowedScopeItemsPerGroup>("test");

    EXPECT_EQ(2, *attr.getAllowedCount(oddGroup, oddScope));
    EXPECT_EQ(0, *attr.getAllowedCount(oddGroup, evenScope));

    // There is no evenGroup objects in unassigned
    EXPECT_FALSE(attr.getAllowedCount(evenGroup, evenScope));
    EXPECT_FALSE(attr.getAllowedCount(evenGroup, oddScope));
  }
}

} // namespace facebook::rebalancer::packer::tests
