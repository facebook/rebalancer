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

#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventHolder.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include <folly/io/async/Request.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace ::testing;
using namespace facebook::algopt::treeprof;

TEST(EventRecorderTest, NoContext) {
  const EventRecorder recorderWithNoContext("test");
  EXPECT_NE(std::nullopt, recorderWithNoContext.errorMessage());
  EXPECT_EQ("RequestContext not set", *recorderWithNoContext.errorMessage());
}

TEST(EventRecorderTest, EmptyContext) {
  const folly::RequestContextScopeGuard request;
  const EventRecorder recorderWithEmptyContext("test");
  EXPECT_NE(std::nullopt, recorderWithEmptyContext.errorMessage());
  EXPECT_EQ(
      "EventRecorder must be created inside the scope of a profiler",
      *recorderWithEmptyContext.errorMessage());
}

TEST(EventRecorderTest, MissingParent) {
  const folly::RequestContextScopeGuard request;
  folly::RequestContext::get()->setContextData(
      EventHolder::key(),
      std::make_unique<EventHolder>(std::shared_ptr<Event>{}));
  const EventRecorder recorderWithMissingParent("test");
  EXPECT_NE(std::nullopt, recorderWithMissingParent.errorMessage());
  EXPECT_EQ(
      "EventRecorder is missing a parent event",
      *recorderWithMissingParent.errorMessage());
}

TEST(EventRecorderTest, Basic) {
  const folly::RequestContextScopeGuard request;
  auto event = std::make_shared<Event>("root");
  folly::RequestContext::get()->setContextData(
      EventHolder::key(), std::make_unique<EventHolder>(event));

  {
    const EventRecorder a("a");
    {
      const EventRecorder b("b");
      const EventRecorder c("c");
    }
    const EventRecorder d("d");
  }

  EXPECT_EQ("root", event->getName());
  EXPECT_EQ(1, event->getChildren().size());

  auto a = event->getChildren().at(0);
  EXPECT_EQ("a", a->getName());
  EXPECT_EQ(2, a->getChildren().size());

  auto b = a->getChildren().at(0);
  EXPECT_EQ("b", b->getName());
  EXPECT_EQ(1, b->getChildren().size());

  auto c = b->getChildren().at(0);
  EXPECT_EQ("c", c->getName());
  EXPECT_EQ(0, c->getChildren().size());

  auto d = a->getChildren().at(1);
  EXPECT_EQ("d", d->getName());
  EXPECT_EQ(0, d->getChildren().size());
}
