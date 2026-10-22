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

#include "algopt/rebalancer/interface/ProblemSolver.h"

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::interface {

#define REBALANCER_EXPECT_RUNTIME_ERROR(code, message) \
  EXPECT_THAT(                                         \
      [&]() { code; }, testing::ThrowsMessage<std::runtime_error>(message))

#define REBALANCER_EXPECT_EQ_ASSIGNMENT(expected, actual) \
  verifyAssignmentsAreAsExpected(__LINE__, __FILE__, expected, actual)

constexpr int defaultExecutorThreadCount = 20;

// Common test thread counts for parameterized tests
inline auto testThreadCounts() {
  return ::testing::Values(1, 2, 50);
}

template <class Key, class Value>
std::map<Key, Value> toOrderedMap(const folly::F14FastMap<Key, Value>& map) {
  return std::map<Key, Value>(map.begin(), map.end());
}

AssignmentAffinity makeAssignmentAffinity(
    std::string objectName,
    std::string scopeItemName,
    double affinity);

AvoidAssignment makeAvoidAssignment(
    std::string object,
    std::vector<::std::string> scopeItems);

LocalSearchSolverSpec makeDefaultLocalSearchSolver(
    int moveLimit = std::numeric_limits<int>::max(),
    int timeLimit = std::numeric_limits<int>::max());

enum SolverAlgoType {
  LOCALSEARCH,
  OPTIMAL,
};
bool isOptimal(const SolverAlgoType solverAlgoType);

// Internal tests whose expected objectives depend on it pin
// RolloutConfig::useSeparatedLocalSearchPenaltyObjective to true. OSS builds
// keep the Thrift default, which adds continuous local-search penalties to the
// reported Global objective.
#ifdef REBALANCER_OSS_BUILD
inline constexpr bool kSeparatedLocalSearchPenaltyObjective = false;
#else
inline constexpr bool kSeparatedLocalSearchPenaltyObjective = true;
#endif

constexpr double expectedLocalSearchGlobalObjective(
    double globalValue,
    double penaltyValue) {
  return kSeparatedLocalSearchPenaltyObjective ? globalValue
                                               : globalValue + penaltyValue;
}

std::shared_ptr<folly::CPUThreadPoolExecutor> getTestExecutor(
    int numThreads = defaultExecutorThreadCount);

struct TestProblemSolverParams {
  bool useParallelMaterializer = true;
  bool canExecuteAsync = true;
  int executorThreadCount = defaultExecutorThreadCount;
};
std::unique_ptr<ProblemSolver> initializeTestProblemSolver(
    const TestProblemSolverParams& params = TestProblemSolverParams());

template <typename ContainerToObjectsMap>
void verifyAssignmentsAreAsExpected(
    int lineNumber, /*line number from which this function was called; for
                      printing purposes*/
    const std::string&
        fileName, /*file name from which this function was called;*/
    const ContainerToObjectsMap& assignment,
    const AssignmentSolution& solution) {
  std::map<std::string, std::string> expectedAssignment;
  for (const auto& [container, objects] : assignment) {
    for (const auto& object : objects) {
      expectedAssignment[object] = container;
    }
  }
  EXPECT_EQ(expectedAssignment, toOrderedMap(*solution.assignment()))
      << " assignment mismatch at line " << lineNumber << " in file "
      << fileName;
}

template <typename T>
inline const T& getSummary(
    const std::vector<T>& summaries,
    const std::string& name) {
  auto it =
      std::find_if(summaries.begin(), summaries.end(), [&name](const T& obj) {
        return *obj.name() == name;
      });
  if (it == summaries.end()) {
    throw std::runtime_error(
        fmt::format(
            "Given name '{}' not found in list of objective/constraint summaries",
            name));
  }
  return *it;
}

inline const SingleObjectiveSummary& getObjectiveSummary(
    const std::vector<SingleObjectiveSummary>& objectives,
    const std::string& objName) {
  return getSummary(objectives, objName);
}

inline const SingleConstraintSummary& getConstraintSummary(
    const std::vector<SingleConstraintSummary>& constraints,
    const std::string& constraintName) {
  return getSummary(constraints, constraintName);
}

inline double getObjectiveValue(
    const std::vector<SingleObjectiveSummary>& objectives,
    const std::string& objName) {
  const auto& summary = getObjectiveSummary(objectives, objName);
  return *summary.value();
}

} // namespace facebook::rebalancer::interface
