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

#include <memory>

namespace facebook::algopt::lp {

class ConstraintImpl {
 public:
  virtual ~ConstraintImpl();

  /// LP sensitivity accessors. The default implementations throw; a backend
  /// that can report solved-basis information overrides them.
  ///
  /// All three are only meaningful after a solve that reached an optimal LP
  /// solution. Reading them before a solve, or after a failed one, gives
  /// whatever the backend happens to be holding. A backend that solves
  /// multiple objectives one after another reports them for the last
  /// objective's LP, which includes rows holding every earlier objective at
  /// its optimum.
  ///
  /// Duals are defined for LPs only. On a MIP the backend will raise its own
  /// error; that error is deliberately allowed to propagate rather than being
  /// mapped to a sentinel value.
  ///
  /// A degenerate LP has more than one optimal dual vector, and two backends
  /// may each return a different valid one for the same primal solution.
  /// Column generation and Benders decomposition are correct under any valid
  /// dual, but their iterates are NOT reproducible across backends (or across
  /// solver versions) for such a problem. Do not assert on exact dual values.
  ///
  /// Sign convention: the BCL/Xpress convention for a MINIMIZATION problem,
  /// which every backend in this library uses (none of them flips the
  /// objective sense). A `>=` row has a non-negative dual, a `<=` row a
  /// non-positive one, and the dual is the rate of change of the objective
  /// per unit increase of the RHS.
  virtual double getDual() const;

  /// Row slack, defined as RHS minus row activity, matching Xpress and
  /// Gurobi. See getDual() for when the value is meaningful.
  virtual double getSlack() const;

  /// The right-hand side this row was built with. For a one-sided row this is
  /// the finite bound; for an equality row the common bound.
  virtual double getRHS() const;
};

class Constraint {
 public:
  Constraint() = delete;
  explicit Constraint(std::shared_ptr<ConstraintImpl> constraint);

  std::shared_ptr<ConstraintImpl> get();

  /// See the matching ConstraintImpl accessors for semantics and caveats.
  /// Throws if the backend does not support sensitivity information.
  double getDual() const;
  double getSlack() const;
  double getRHS() const;

 private:
  std::shared_ptr<ConstraintImpl> constraint_;
};

} // namespace facebook::algopt::lp
