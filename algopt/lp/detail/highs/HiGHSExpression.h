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

#ifdef REBALANCER_USE_HIGHS

#include "algopt/lp/detail/highs/HiGHSVariable.h"
#include "algopt/lp/generic/Expression.h"

#include <folly/container/F14Map.h>

#include <utility>
#include <vector>

namespace facebook::algopt::lp::detail {

class HiGHSExpression : public ExpressionImpl {
 public:
  HiGHSExpression() = default;
  explicit HiGHSExpression(double coeff);
  HiGHSExpression(const HiGHSVariable& var, double coeff);

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

  size_t size() const;
  const std::vector<HiGHSVariable>& getVariables() const;
  const std::vector<double>& getCoeffs() const;
  double getConstant() const override;
  bool isQuadratic() const;

  using ColPair = std::pair<HighsInt, HighsInt>;
  const folly::F14FastMap<ColPair, double>& getQuadraticCoeffs() const;

  void print() const override;

 private:
  double constant_ = 0.0;
  std::vector<HiGHSVariable> variables_;
  std::vector<double> coeffs_;
  folly::F14FastMap<ColPair, double> quadraticCoeffs_;
};

} // namespace facebook::algopt::lp::detail

#endif
