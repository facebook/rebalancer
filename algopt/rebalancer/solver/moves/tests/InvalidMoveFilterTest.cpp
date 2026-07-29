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

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <gtest/gtest.h>

namespace facebook::rebalancer::tests {

using entities::ContainerId;
using entities::ObjectId;

TEST(InvalidMoveFilterTest, EmptyFilterSkipsNothing) {
  const InvalidMoveFilter filter(/*numObjects=*/100, /*numContainers=*/50);
  EXPECT_TRUE(filter.empty());
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(99), ContainerId(49)));
}

TEST(InvalidMoveFilterTest, MarkInvalid) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/5);

  filter.markInvalid(ObjectId(3), ContainerId(2));

  EXPECT_FALSE(filter.empty());
  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(3), ContainerId(2)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(3), ContainerId(0)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(2)));
}

TEST(InvalidMoveFilterTest, MultiplePairs) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/5);

  filter.markInvalid(ObjectId(0), ContainerId(0));
  filter.markInvalid(ObjectId(9), ContainerId(4));
  filter.markInvalid(ObjectId(5), ContainerId(2));

  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(9), ContainerId(4)));
  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(5), ContainerId(2)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(4)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(9), ContainerId(0)));
}

TEST(InvalidMoveFilterTest, OneObjectInvalidForAllContainers) {
  InvalidMoveFilter filter(/*numObjects=*/5, /*numContainers=*/10);

  for (const auto c : folly::irange(10)) {
    filter.markInvalid(ObjectId(2), ContainerId(c));
  }

  for (const auto c : folly::irange(10)) {
    EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(2), ContainerId(c)));
    EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(c)));
  }
}

TEST(InvalidMoveFilterTest, AllObjectsInvalidForOneContainer) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/3);

  for (const auto o : folly::irange(10)) {
    filter.markInvalid(ObjectId(o), ContainerId(1));
  }

  for (const auto o : folly::irange(10)) {
    EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(o), ContainerId(1)));
    EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(o), ContainerId(0)));
  }
}

TEST(InvalidMoveFilterTest, DuplicateMarkIsIdempotent) {
  InvalidMoveFilter filter(/*numObjects=*/5, /*numContainers=*/5);

  filter.markInvalid(ObjectId(1), ContainerId(3));
  filter.markInvalid(ObjectId(1), ContainerId(3));

  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(1), ContainerId(3)));
}

TEST(InvalidMoveFilterTest, MergeFromUnionsBothFilters) {
  InvalidMoveFilter a(/*numObjects=*/5, /*numContainers=*/5);
  a.markInvalid(ObjectId(1), ContainerId(2));
  a.markInvalid(ObjectId(3), ContainerId(4)); // shared with b

  InvalidMoveFilter b(/*numObjects=*/5, /*numContainers=*/5);
  b.markInvalid(ObjectId(0), ContainerId(0));
  b.markInvalid(ObjectId(3), ContainerId(4)); // shared with a

  a.mergeFrom(b);

  // a's own pairs are kept.
  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(1), ContainerId(2)));
  // b's pairs are added.
  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  // the shared pair stays invalid.
  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(3), ContainerId(4)));
  // an unmarked pair stays valid.
  EXPECT_FALSE(a.isMarkedInvalid(ObjectId(2), ContainerId(2)));
}

TEST(InvalidMoveFilterTest, MergeFromEmptyIsNoOp) {
  InvalidMoveFilter a(/*numObjects=*/3, /*numContainers=*/3);
  a.markInvalid(ObjectId(1), ContainerId(1));

  const InvalidMoveFilter empty(/*numObjects=*/3, /*numContainers=*/3);
  a.mergeFrom(empty);

  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(1), ContainerId(1)));
  EXPECT_FALSE(a.isMarkedInvalid(ObjectId(0), ContainerId(0)));
}

} // namespace facebook::rebalancer::tests
