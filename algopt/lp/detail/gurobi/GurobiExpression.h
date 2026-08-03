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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/gurobi/gurobi.h"
#include "algopt/lp/generic/Expression.h"

#include <variant>

namespace facebook::algopt::lp::detail {

class GurobiExpression : public ExpressionImpl {
 public:
  explicit GurobiExpression(const GRBLinExpr& expression);
  explicit GurobiExpression(
      const std::variant<GRBLinExpr, GRBQuadExpr>& expression);

  std::shared_ptr<ExpressionImpl> clone() const override;

  void add(double constant) override;
  void add(std::shared_ptr<const ExpressionImpl> expression) override;

  void multiply(double constant) override;
  void multiply(std::shared_ptr<const ExpressionImpl> expression) override;

  std::shared_ptr<RelationImpl> makeEqualZeroRelation() const override;
  std::shared_ptr<RelationImpl> makeLessEqualZeroRelation() const override;
  std::shared_ptr<RelationImpl> makeGreaterEqualZeroRelation() const override;

  double getValue() const override;
  double computeValue() const override;
  double getConstant() const override;

  void print() const override;

  const std::variant<GRBLinExpr, GRBQuadExpr>& get() const;

 private:
  std::variant<GRBLinExpr, GRBQuadExpr> expression_;
};

} // namespace facebook::algopt::lp::detail

#endif
