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

#include "algopt/lp/generic/thrift/gen-cpp2/problem_types.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/solvers/Solver.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <optional>
#include <stddef.h>
#include <string>
#include <vector>

namespace facebook::rebalancer {

class OptimalSolver : public Solver {
 public:
  OptimalSolver(
      const interface::OptimalSolverSpec& configs =
          interface::OptimalSolverSpec());
  bool solve(Problem& p, Profile profile = std::nullopt) override;
  void solve(
      Problem& p,
      PackerSet<entities::ContainerId> dynamicContainers,
      Profile profile = std::nullopt);

  /** using only the objects in @param dynamic_containers as objects and @param
   * dynamic_containers as containers, build and solve a MIP model and return
   * the resulting set of changes to apply to initial solution **/
  ChangeSet findOptimalChangeSet(
      Problem& p,
      PackerSet<entities::ContainerId> dynamic_containers,
      Profile profile = std::nullopt);

  static double getFeasibilityTolerance(Problem& p);

  size_t getNumDynamicEquivalenceSets() const;

 private:
  /** builds and solve the MIP model, the result is stored in p.lp_store
   * variables **/
  interface::EndReason
  buildAndSolveMIPModel(Problem& p, LpContext& context, Profile profile);

  static void addVariableStartValues(Problem& p, LpContext& context);

  void initialize(Problem& p);

  void setUpObjectives(Problem& p, LpContext& context);

  std::vector<algopt::lp::thrift::ProblemResult> mipOptimize(
      Problem& p,
      LpContext& context);

  static void updateSolverProfile(
      std::vector<algopt::lp::thrift::ProblemResult>& resultPerObjective,
      interface::OptimalSolverProfile& solverProfile,
      algopt::lp::thrift::ProblemStatus problemStatus);

  static ChangeSet createChangesFromSolution(
      Problem& p,
      const PackerSet<entities::ContainerId>& dynamic_containers,
      const PackerSet<entities::EquivalenceSetId>& dynamic_equiv_sets);

  void applyChanges(Problem& p, const ChangeSet& changes);

  static ChangeSet reverseChanges(const ChangeSet& changes);

  interface::OptimalSolverSpec configs;
  size_t num_dynamic_equivalence_sets = 0;

  void setUpObjectivesAndConstraints(Problem& problem, LpContext& context)
      const;
  const std::vector<ExprPtr> getObjectiveExprs(Problem& problem) const;
  static const PackerMap<ExprPtr, std::string> getConstraintExprs(
      Problem& problem,
      LpContext& context);
  void addObjectivesToModel(
      Problem& problem,
      LpContext& context,
      const std::vector<ExprPtr>& objectiveExprNodes) const;
  static void addObjectInContainerConstraints(Problem& p, LpContext& context);

  void saveDebugDataPreSolve(Problem& p) const;
  void saveDebugDataPostSolve(Problem& p) const;
};

PackerMap<int, int> createObjAssignFromEquivAssign(
    PackerMap<int, PackerMap<int, std::vector<int>>> cont_to_equiv_set_map,
    const PackerMap<int, PackerMap<int, int>>& equiv_set_to_cont_maps);
} // namespace facebook::rebalancer
