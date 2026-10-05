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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSetsMatching.h"

#include <folly/container/F14Map.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Hint.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;
namespace facebook::rebalancer::packer::tests {

using entities::ContainerId;
using entities::EquivalenceSetId;
using entities::ObjectId;
TEST(EquivalenceSetsMatchingTest, Identity) {
  EquivalenceSetInfo equivalenceSetsLeft, equivalenceSetsRight;

  EquivalenceSetMetadata set1, set2;
  set1.name() = '1';
  set1.objectNames()->emplace_back("101");
  set1.objectNames()->emplace_back("102");
  set2.name() = '2';
  set2.objectNames()->emplace_back("103");
  set2.objectNames()->emplace_back("104");

  equivalenceSetsLeft.equivalenceSets()->push_back(set1);
  equivalenceSetsLeft.equivalenceSets()->push_back(set2);

  equivalenceSetsRight.equivalenceSets()->push_back(set1);
  equivalenceSetsRight.equivalenceSets()->push_back(set2);

  EquivalenceSetsMatching match =
      facebook::rebalancer::interface::EquivalenceSetsMatching(
          equivalenceSetsLeft, equivalenceSetsRight);
  const folly::F14FastMap<std::string, std::string> matching =
      match.heavyEdgeMatching();

  for (const auto& [left, right] : matching) {
    EXPECT_EQ(left, right);
  }
}

TEST(EquivalenceSetsMatchingTest, Simple) {
  EquivalenceSetInfo equivalenceSetsLeft, equivalenceSetsRight;

  EquivalenceSetMetadata set1, set2, set3, set4;
  set1.name() = '1';
  set1.objectNames()->emplace_back("101");
  set1.objectNames()->emplace_back("102");
  set1.objectNames()->emplace_back("103");
  set2.name() = '2';
  set2.objectNames()->emplace_back("104");

  equivalenceSetsLeft.equivalenceSets()->push_back(set1);
  equivalenceSetsLeft.equivalenceSets()->push_back(set2);

  set3.name() = '3';
  set3.objectNames()->emplace_back("101");
  set4.name() = '4';
  set4.objectNames()->emplace_back("102");
  set4.objectNames()->emplace_back("103");

  equivalenceSetsRight.equivalenceSets()->push_back(set2); // common to both
  equivalenceSetsRight.equivalenceSets()->push_back(set3);
  equivalenceSetsRight.equivalenceSets()->push_back(set4);

  EquivalenceSetsMatching match =
      facebook::rebalancer::interface::EquivalenceSetsMatching(
          equivalenceSetsLeft, equivalenceSetsRight);
  folly::F14FastMap<std::string, std::string> matching =
      match.heavyEdgeMatching();

  // object 104 is common
  EXPECT_EQ(matching.at("2"), "2");
  // objects 102,103 maps to 4
  EXPECT_EQ(matching.at("1"), "4");
}

TEST(EquivalenceSetsMatchingTest, LeftSetOrderedByHeavyWeightFirst) {
  EquivalenceSetInfo equivalenceSetsLeft, equivalenceSetsRight;

  EquivalenceSetMetadata set1, set2, set3, set4;
  set1.name() = "set1";
  set1.objectNames()->emplace_back("101");
  set1.objectNames()->emplace_back("102");
  set2.name() = "set2";
  set2.objectNames()->emplace_back("103");
  set2.objectNames()->emplace_back("104");
  set2.objectNames()->emplace_back("105");
  equivalenceSetsLeft.equivalenceSets()->emplace_back(set1);
  equivalenceSetsLeft.equivalenceSets()->emplace_back(set2);

  set3.name() = "set3";
  set3.objectNames()->emplace_back("101");
  set3.objectNames()->emplace_back("102");
  set3.objectNames()->emplace_back("103");
  set3.objectNames()->emplace_back("104");
  set4.name() = "set4";
  set4.objectNames()->emplace_back("105");
  equivalenceSetsRight.equivalenceSets()->emplace_back(set3);
  equivalenceSetsRight.equivalenceSets()->emplace_back(set4);

  // set1 and set3 have 2 common objects out of 4 distinct objects.
  // set2 and set3 have 2 common objects out of 5 distinct objects.
  // set2 and set4 have 1 common objects out of 3 distinct objects.
  // The sorted bipartite graph should look like this:
  // {"set1" : {"set3", 0.5}}
  // {"set2" : {"set3", 0.4}, {"set4", 0.33}}
  EquivalenceSetsMatching match =
      facebook::rebalancer::interface::EquivalenceSetsMatching(
          equivalenceSetsLeft, equivalenceSetsRight);
  folly::F14FastMap<std::string, std::string> matching =
      match.heavyEdgeMatching();
  ASSERT_TRUE(matching.contains("set1"));
  ASSERT_TRUE(matching.contains("set2"));
  EXPECT_EQ(matching.at("set1"), "set3");
  EXPECT_EQ(matching.at("set2"), "set4");
}

TEST(EquivalenceSetsMatchingTest, Example1) {
  EquivalenceSetInfo equivalenceSetsLeft, equivalenceSetsRight;

  EquivalenceSetMetadata set1, set2, set3, set4;
  set1.name() = "h1";
  set1.objectNames()->emplace_back("s1");
  set1.objectNames()->emplace_back("s2");
  set1.objectNames()->emplace_back("s3");
  set1.objectNames()->emplace_back("s4");
  set1.objectNames()->emplace_back("s5");

  set2.name() = "h2";

  equivalenceSetsLeft.equivalenceSets()->push_back(set1);
  equivalenceSetsLeft.equivalenceSets()->push_back(set2);

  set3.name() = "h3";
  set3.objectNames()->emplace_back("s1");
  set3.objectNames()->emplace_back("s2");
  set3.objectNames()->emplace_back("s3");
  set4.name() = "h4";
  set4.objectNames()->emplace_back("s4");
  set4.objectNames()->emplace_back("s5");

  equivalenceSetsRight.equivalenceSets()->push_back(set3);
  equivalenceSetsRight.equivalenceSets()->push_back(set4);

  EquivalenceSetsMatching match =
      facebook::rebalancer::interface::EquivalenceSetsMatching(
          equivalenceSetsLeft, equivalenceSetsRight);
  folly::F14FastMap<std::string, std::string> matching =
      match.heavyEdgeMatching();

  ASSERT_TRUE(matching.contains("h1"));
  EXPECT_EQ(matching.at("h1"), "h3");
  EXPECT_FALSE(matching.contains("h2")); // not matched to anyone
}

} // namespace facebook::rebalancer::packer::tests
