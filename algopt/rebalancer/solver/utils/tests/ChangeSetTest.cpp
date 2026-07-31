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

#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"

#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

TEST(ChangeSetTest, GetInverse) {
  ChangeSet changes;
  changes.insert(Change(object(1), container(10), -1));
  changes.insert(Change(object(1), container(11), 1));
  changes.insert(Change(object(2), container(12), -1));
  changes.insert(Change(object(2), container(13), 1));

  const auto inverse = changes.getInverse();
  const std::vector<Change> expected = {
      Change(object(2), container(13), -1),
      Change(object(2), container(12), 1),
      Change(object(1), container(11), -1),
      Change(object(1), container(10), 1)};

  ASSERT_EQ(expected.size(), inverse.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected.at(i).getObject(), inverse.at(i).getObject());
    EXPECT_EQ(expected.at(i).getContainer(), inverse.at(i).getContainer());
    EXPECT_EQ(expected.at(i).getValue(), inverse.at(i).getValue());
  }
}

} // namespace facebook::rebalancer::packer::tests
