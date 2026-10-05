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
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include "gmock/gmock.h"
#include <gtest/gtest.h>

#include <string>

namespace facebook::rebalancer::packer::tests {

PackerSet<entities::ContainerId> one = {container(1)};
PackerSet<entities::ContainerId> three = {container(3)};

TEST(BoundConstraintsTests, Preconditions) {
  REBALANCER_EXPECT_RUNTIME_ERROR(
      []() {
        BoundConstraints::Builder{}
            .setNotGivingContainers({container(1)})
            .setNotTakingContainers({container(1)})
            .setDynamicContainers({container(1)})
            .build();
      }(),
      ("cannot specify bound constraints with both dynamic containers "
       "constraint as well as not_giving and not_taking containers constraint"));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      []() {
        BoundConstraints::Builder{}
            .setNotGivingContainers({container(1)})
            .setDynamicContainers({container(1)})
            .build();
      }(),
      ("cannot specify bound constraints with both dynamic containers "
       "constraint as well as not_giving containers constraint"));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      []() {
        BoundConstraints::Builder{}
            .setNotTakingContainers({container(1)})
            .setDynamicContainers({container(1)})
            .build();
      }(),
      ("cannot specify bound constraints with both dynamic containers "
       "constraint as well as not_taking containers constraint"));

  // cannot build same object twice
  REBALANCER_EXPECT_RUNTIME_ERROR(
      []() {
        BoundConstraints::Builder builder;
        builder.build();
        builder.build();
      }(),
      "please create a new BoundConstraintsBuilder: build already called on the current instance");

  EXPECT_TRUE(BoundConstraints().isEmpty());
}

TEST(BoundConstraintsTests, DynamicContainer) {
  auto builder =
      BoundConstraints::Builder{}.setDynamicContainers({container(1)});
  auto bc = builder.build();
  EXPECT_TRUE(bc.giving(container(1)));
  EXPECT_FALSE(bc.giving(container(2)));
  EXPECT_TRUE(bc.taking(container(1)));
  EXPECT_FALSE(bc.taking(container(2)));
}

TEST(BoundConstraintsTests, NotGivingContainer) {
  auto builder =
      BoundConstraints::Builder{}.setNotGivingContainers({container(1)});
  auto bc = builder.build();
  EXPECT_FALSE(bc.giving(container(1)));
  EXPECT_TRUE(bc.giving(container(2)));
  EXPECT_TRUE(bc.taking(container(1)));
  EXPECT_TRUE(bc.taking(container(2)));
}

TEST(BoundConstraintsTests, NotTakingContainer) {
  auto builder =
      BoundConstraints::Builder{}.setNotTakingContainers({container(1)});
  auto bc = builder.build();
  EXPECT_TRUE(bc.giving(container(1)));
  EXPECT_TRUE(bc.giving(container(2)));
  EXPECT_FALSE(bc.taking(container(1)));
  EXPECT_TRUE(bc.taking(container(2)));
}

TEST(BoundConstraintsTests, NotGivingAndNotTakingContainer) {
  auto builder = BoundConstraints::Builder{}
                     .setNotGivingContainers({container(1)})
                     .setNotTakingContainers({container(1)});
  auto bc = builder.build();
  EXPECT_FALSE(bc.giving(container(1)));
  EXPECT_TRUE(bc.giving(container(2)));
  EXPECT_FALSE(bc.taking(container(1)));
  EXPECT_TRUE(bc.taking(container(2)));
}

TEST(BoundConstraintsTests, MultipleDynamicContainers) {
  auto builder = BoundConstraints::Builder{}.setDynamicContainers(
      {container(1), container(2)});
  auto bc = builder.build();

  EXPECT_TRUE(bc.anyGiving(one));
  EXPECT_TRUE(bc.anyGiving(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_TRUE(bc.anyGiving(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)}));
  EXPECT_FALSE(bc.anyGiving(three));
  EXPECT_FALSE(bc.anyGiving(
      PackerSet<entities::ContainerId>{
          container(3), container(4), container(5)}));

  EXPECT_TRUE(bc.anyTaking(one));
  EXPECT_TRUE(bc.anyTaking(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_TRUE(bc.anyTaking(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)}));
  EXPECT_FALSE(bc.anyTaking(three));
  EXPECT_FALSE(bc.anyTaking({container(3), container(4), container(5)}));
}

TEST(BoundConstraintsTests, MultipleNonGivingContainers) {
  auto builder = BoundConstraints::Builder{}.setNotGivingContainers(
      {container(1), container(2)});
  auto bc = builder.build();
  EXPECT_TRUE(bc.anyTaking(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_FALSE(bc.anyGiving(one));
  EXPECT_FALSE(bc.anyGiving(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_TRUE(bc.anyGiving(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)}));
  EXPECT_TRUE(bc.anyGiving(three));
}

TEST(BoundConstraintsTests, MultipleNonTakingContainers) {
  auto builder = BoundConstraints::Builder{}.setNotTakingContainers(
      {container(1), container(2)});
  auto bc = builder.build();
  EXPECT_TRUE(bc.anyGiving(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_FALSE(bc.anyTaking(one));
  EXPECT_FALSE(bc.anyTaking(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_TRUE(bc.anyTaking(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)}));
  EXPECT_TRUE(bc.anyTaking(three));
}

TEST(BoundConstraintsTests, MultipleNonGivingAndNonTakingContainers) {
  auto builder = BoundConstraints::Builder{}
                     .setNotGivingContainers({container(1), container(2)})
                     .setNotTakingContainers({container(1), container(2)});
  auto bc = builder.build();
  EXPECT_FALSE(bc.anyGiving(one));
  EXPECT_FALSE(bc.anyGiving(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_TRUE(bc.anyGiving({container(1), container(2), container(3)}));
  EXPECT_TRUE(bc.anyGiving(three));

  EXPECT_FALSE(bc.anyTaking(one));
  EXPECT_FALSE(bc.anyTaking(
      PackerSet<entities::ContainerId>{container(1), container(2)}));
  EXPECT_TRUE(bc.anyTaking({container(1), container(2), container(3)}));
  EXPECT_TRUE(bc.anyTaking(three));
}

TEST(BoundConstraintsTests, ThrowWhenCalledWithEmptyBoundConstraints) {
  auto bc = BoundConstraints();
  REBALANCER_EXPECT_RUNTIME_ERROR(
      bc.giving(container(0)),
      "Expected to be called only when BoundConstraints is not empty.");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      bc.taking(container(0)),
      "Expected to be called only when BoundConstraints is not empty.");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      bc.anyGiving(PackerSet<entities::ContainerId>{}),
      "Expected to be called only when BoundConstraints is not empty.");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      bc.anyTaking(PackerSet<entities::ContainerId>{}),
      "Expected to be called only when BoundConstraints is not empty.");
}

} // namespace facebook::rebalancer::packer::tests
