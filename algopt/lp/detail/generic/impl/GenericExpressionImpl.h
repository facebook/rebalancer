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

#include "algopt/lp/detail/generic/impl/GenericVariableImpl.h"
#include "algopt/lp/generic/Expression.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>

#include <memory>
#include <set>
#include <stdexcept>

namespace facebook::algopt::lp::detail {

class GenericVariableImpl;

/** The following enums decide how image(...) is taken. We are given a list
 of allowed partitions P, say x lies in P but y, z don't
 Expression: 3x + 4y + 5z - 5

 We have three set of rules affecting each of the above terms.
 AllowedTerm : 3x (x lies in P)
 Remaining Term : 4y + 5z
 Constant Term : -5

AllowedTerms::COPY: copies the allowedTerm part of expression as is
    results in 3x
AllowedTerms::FULLY_CONSTRAINT: creates an expression for current slack
    results in : 3x - 3 * x.getValue();
Enums fo remaining term and constant term are self explanatory

image(3x + 4y + 5z - 5, RealizationRule(AllowedTerms::COPY,
RemainingTerms::SUBSTITUTE_CURR_VALUE, ConstantTerm::Retain))

= 3x + 4 * y.value() + 5 * z.value() - 5

*/

class RealizationRule {
 public:
  enum class AllowedTerms {
    COPY = 0,
    FULLY_CONSTRAINED = 1,
  };

  enum class RemainingTerms {
    OMIT = 0,
    SUBSTITUTE_CURR_VALUE = 1,
  };

  enum class ConstantTerm {
    RETAIN = 0,
    DROP = 1,
  };

  RealizationRule() = default;

  /** Most commonly used orderings
   * Can add more overloads for convenience as we need them
   */
  explicit RealizationRule(
      AllowedTerms allowedRule,
      ConstantTerm constantRule = ConstantTerm::RETAIN,
      RemainingTerms remainingRule = RemainingTerms::OMIT);

  explicit RealizationRule(
      RemainingTerms remainingRule,
      ConstantTerm constantRule = ConstantTerm::RETAIN,
      AllowedTerms allowedRule = AllowedTerms::COPY);

  explicit RealizationRule(ConstantTerm constantRule);

  AllowedTerms allowedTermsRule = AllowedTerms::COPY;
  RemainingTerms remainingTermsRule = RemainingTerms::OMIT;
  ConstantTerm constantTermRule = ConstantTerm::RETAIN;
};

constexpr double EPSILON = 1e-8;

using ConstGenericVarPtr = std::shared_ptr<const GenericVariableImpl>;

using ConstGenericVarPtrPair =
    std::pair<ConstGenericVarPtr, ConstGenericVarPtr>;

class GenericExpressionImpl : public ExpressionImpl {
 public:
  explicit GenericExpressionImpl(double constant = 0);
  explicit GenericExpressionImpl(
      double coeff,
      std::shared_ptr<const GenericVariableImpl> var);

  virtual std::shared_ptr<ExpressionImpl> clone() const override;

  void add(double constant) override;
  void add(std::shared_ptr<const ExpressionImpl> expression) override;

  void multiply(double constant) override;

  void multiply(std::shared_ptr<const ExpressionImpl> expression) override;

  virtual std::shared_ptr<RelationImpl> makeEqualZeroRelation() const override;
  virtual std::shared_ptr<RelationImpl> makeLessEqualZeroRelation()
      const override;
  virtual std::shared_ptr<RelationImpl> makeGreaterEqualZeroRelation()
      const override;

  double getValue() const override;
  double computeValue() const override;

  double getConstant() const override;
  bool isConstant() const;
  bool isQuadratic() const;

  void print() const override;

  std::optional<PartitionId> getPartitionId() const;

  /* For the linear terms SUM a_i x_i, this returns the map x_i -> a_i */
  const folly::F14FastMap<ConstGenericVarPtr, double>& getLinearCoeffMap()
      const;

  /* For the quadratic terms SUM a_i x_i x_j, this returns the
    mapping <x_i, x_j> -> a_i */
  const std::optional<folly::F14FastMap<ConstGenericVarPtrPair, double>>&
  getQuadraticCoeffMap() const;

  static ConstGenericVarPtrPair makeOrderedPair(
      ConstGenericVarPtr x,
      ConstGenericVarPtr y);

  const folly::F14FastMap<ConstGenericVarPtr, double>& getCoeffMap() const;

  std::string digest(bool showValues = false, size_t limit = 30) const;

  // adds delta to linearCoeff_[var]
  void add(ConstGenericVarPtr var, double delta);
  // adds delta to quadraticCoeff_[var]
  void add(const ConstGenericVarPtrPair& var, double delta);

  /** realize this expression using variable mapping in @param oldVarsToNew */
  Expression realize(
      const folly::F14FastMap<ConstGenericVarPtr, Variable>& oldVarsToNew)
      const;

  void setupPartitionIndex();

  Expression image(
      const folly::F14FastSet<PartitionId>& allowedPartitions,
      RealizationRule realizationRule) const;

  Expression image(RealizationRule::ConstantTerm constantRule) const;

  // returns the number of positive, negative and zero coefficients in the
  // expression
  std::tuple<int, int, int> getNumCoeffParity() const;

  thrift::GenericExpression toThrift(
      const folly::F14FastMap<ConstGenericVarPtr, int64_t>& varToId) const;

  static std::shared_ptr<GenericExpressionImpl> fromThrift(
      thrift::GenericExpression expr,
      const folly::F14FastMap<int64_t, ConstGenericVarPtr>& idToVar);

 private:
  /** override the contents of this expression by @param expr */
  void overwrite(const std::shared_ptr<const GenericExpressionImpl>& expr);

  // Expression = LINEAR_COMPONENT + QUADRATIC_COMPONENT
  // LINEAR_COMPONENT = sum_i (coeff(i) * i) + constant_
  folly::F14FastMap<ConstGenericVarPtr, double> linearCoeff_;
  double constant_;
  // QUADRATIC_COMPONENT = sum_j (coeff(j) * j.first * j.second)
  // make this field optional to speed up operations for linear expressions,
  // which is the more common situation
  std::optional<folly::F14FastMap<ConstGenericVarPtrPair, double>>
      quadraticCoeff_;

  std::optional<folly::F14FastMap<
      PartitionId,
      folly::F14FastMap<ConstGenericVarPtr, double>>>
      partitionIndex_;
};

} // namespace facebook::algopt::lp::detail
