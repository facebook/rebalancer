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
#include "algopt/rebalancer/common/log/LogCollector.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace facebook::rebalancer {
namespace {

class RecordingLog final : public RebalancerLog {
 public:
  void log(const GenericInfo& info) override {
    genericInfo = info;
  }

  std::optional<GenericInfo> genericInfo;
};

TEST(LogCollectorTest, ForwardsDataToEveryLog) {
  const auto firstLog = std::make_shared<RecordingLog>();
  const auto secondLog = std::make_shared<RecordingLog>();
  LogCollector collector(
      std::vector<std::shared_ptr<RebalancerLog>>{firstLog, secondLog});
  const GenericInfo info{.key = "key", .value = 3.0};

  collector.log(info);

  for (const auto& log : {firstLog, secondLog}) {
    ASSERT_TRUE(log->genericInfo.has_value());
    EXPECT_EQ(log->genericInfo->key, info.key);
    EXPECT_EQ(log->genericInfo->value, info.value);
  }
}

TEST(LogCollectorTest, RejectsNullLogs) {
  const std::shared_ptr<RebalancerLog> nullLog;
  std::vector<std::shared_ptr<RebalancerLog>> logsWithNull{
      std::make_shared<RecordingLog>(), nullLog};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      LogCollector{nullLog},
      "Expected loggers passed to LogCollector to be non-null");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      LogCollector{std::move(logsWithNull)},
      "Expected loggers passed to LogCollector to be non-null");
}

} // namespace
} // namespace facebook::rebalancer
