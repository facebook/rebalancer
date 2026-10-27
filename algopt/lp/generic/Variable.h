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
#include <optional>

namespace facebook::algopt::lp {

class ExpressionImpl;

class VariableImpl {
 public:
  enum class Type {
    BINARY,
    CONTINUOUS,
    INTEGER,
    SEMI_CONTINUOUS,
    SEMI_INTEGER,
    UNSET,
  };
  virtual ~VariableImpl();

  virtual void setLB(double lb) = 0;
  virtual void setUB(double ub) = 0;
  virtual void setThreshold(double threshold) = 0;
  virtual bool hasThreshold() const;

  // So far, variable partitioning is only implemented in Gurobi
  virtual void setPartition(int partitionId);

  virtual double getValue() const = 0;

  /// Reduced cost of this column. The default implementation throws; a backend
  /// that can report solved-basis information overrides it.
  ///
  /// Only meaningful after a solve that reached an optimal LP solution, and
  /// only defined for LPs -- on a MIP the backend raises its own error, which
  /// is deliberately allowed to propagate. As with duals, a degenerate LP has
  /// non-unique reduced costs, so do not compare them across backends.
  ///
  /// Sign convention: the BCL/Xpress convention for a MINIMIZATION problem
  /// (every backend here minimizes). A variable at its lower bound has a
  /// non-negative reduced cost, one at its upper bound a non-positive one, and
  /// a basic variable's is zero.
  virtual double getReducedCost() const;

  virtual std::shared_ptr<ExpressionImpl> makeExpression(
      double coeff) const = 0;

 protected:
  Type type_ = Type::UNSET;
};

class Variable {
 public:
  explicit Variable(std::shared_ptr<VariableImpl> variable);

  void setLB(double lb);
  void setUB(double ub);
  void setThreshold(double threshold);
  void setBounds(double lb, double ub);
  void setPartition(int partitionId);

  double getValue() const;

  /// See VariableImpl::getReducedCost for semantics and caveats.
  /// Throws if the backend does not support sensitivity information.
  double getReducedCost() const;

  std::shared_ptr<const VariableImpl> get() const;

 private:
  std::shared_ptr<VariableImpl> variable_;
};

} // namespace facebook::algopt::lp
