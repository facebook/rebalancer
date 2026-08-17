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

#include "algopt/rebalancer/interface/tests/utils.h"

#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

namespace facebook::rebalancer::interface {

AssignmentAffinity makeAssignmentAffinity(
    std::string objectName,
    std::string scopeItemName,
    double affinity) {
  AssignmentAffinity assignmentAffinity;
  assignmentAffinity.objectName() = objectName;
  assignmentAffinity.scopeItemName() = scopeItemName;
  assignmentAffinity.affinity() = affinity;

  return assignmentAffinity;
}

AvoidAssignment makeAvoidAssignment(
    std::string object,
    std::vector<::std::string> scopeItems) {
  AvoidAssignment avoidAssignment;
  avoidAssignment.object() = object;
  avoidAssignment.scopeItems() = scopeItems;

  return avoidAssignment;
}

LocalSearchSolverSpec makeDefaultLocalSearchSolver(
    int moveLimit,
    int timeLimit) {
  LocalSearchSolverSpec spec;
  spec.stopAfterMoves() = moveLimit;
  spec.solveTime() = timeLimit;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
  return spec;
}

bool isOptimal(const SolverAlgoType solverAlgoType) {
  if (solverAlgoType == OPTIMAL) {
    return true;
  }
  return false;
}

std::shared_ptr<folly::CPUThreadPoolExecutor> getTestExecutor(int numThreads) {
  return std::make_shared<folly::CPUThreadPoolExecutor>(
      numThreads, std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
}

std::unique_ptr<ProblemSolver> initializeTestProblemSolver(
    const TestProblemSolverParams& params) {
  auto executor = getTestExecutor(params.executorThreadCount);
  auto solver = ProblemSolverFactory::makeProblemSolver(
      executor,
      "rebalancer",
      "tests",
      /*canExecuteAsync=*/params.canExecuteAsync);
  if (params.useParallelMaterializer) {
    solver->useParallelizedNewMaterializer();
  }
  solver->shouldUseDynamicObjectOrdering(true);

  return solver;
}

} // namespace facebook::rebalancer::interface
