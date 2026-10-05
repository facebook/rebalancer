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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/ContainerSetWithHash.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include "gmock/gmock.h"
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace facebook::rebalancer::packer::tests {

TEST(ContainerSetWithHashTest, BasicEqual) {
  ContainerSetWithHash setWithHash1;
  setWithHash1.set(
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(1),
              container(2),
              container(3),
              container(4),
              container(5),
              container(6)}));

  ContainerSetWithHash setWithHash2;
  setWithHash2.set(
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(3),
              container(1),
              container(4),
              container(5),
              container(6),
              container(2)}));

  // we expect both container sets to have the same hash value although the
  // order of container ids might be different
  EXPECT_TRUE(setWithHash1.getHashValue() == setWithHash2.getHashValue());
}

TEST(ContainerSetWithHashTest, BasicUnequal) {
  ContainerSetWithHash setWithHash1;
  setWithHash1.set(
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(1), container(2), container(3)}));

  ContainerSetWithHash setWithHash2;
  setWithHash2.set(
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(1), container(2), container(4), container(5)}));

  // we expect the hash function to be good enough to at least say sets are
  // unequal in this basic case
  EXPECT_FALSE(setWithHash1.getHashValue() == setWithHash2.getHashValue());
}

TEST(ContainerSetWithHashTest, ErrorWhenSetIsEmpty) {
  // folly::hash::commutative_hash_combine_range() returns 0 as the hash value
  // if the set is empty, but we don't expect this function to be called for
  // empty sets
  ContainerSetWithHash setWithHash1;
  setWithHash1.set(std::make_shared<PackerSet<entities::ContainerId>>());

  EXPECT_TRUE(setWithHash1.isEmpty());

  REBALANCER_EXPECT_RUNTIME_ERROR(
      setWithHash1.getHashValue(),
      "Unexpected attempt to get the hash value of an empty set");
}

} // namespace facebook::rebalancer::packer::tests
