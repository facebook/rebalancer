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

#include "algopt/lp/detail/xpress_v2/XpressV2Types.h"

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress_v2/XpressProblemV2.h"
#include "algopt/lp/generic/SolverError.h"

#include <fmt/format.h>

#include <iostream>
#include <stdexcept>

namespace facebook::algopt::lp::detail {

namespace {

const XpressV2Expression& asV2Expression(const ExpressionImpl& expression) {
  const auto* casted = dynamic_cast<const XpressV2Expression*>(&expression);
  if (casted == nullptr) {
    throw std::invalid_argument(
        "XpressV2 expected an XpressV2Expression; a foreign ExpressionImpl "
        "cannot be combined with the Xpress typed-API backend");
  }
  return *casted;
}

} // namespace

XpressV2Variable::XpressV2Variable(
    const xpress::objects::Variable& variable,
    Type type,
    XpressV2SolutionSlot solution,
    XpressProblemV2* problem)
    : variable_{variable}, solution_{std::move(solution)}, problem_{problem} {
  type_ = type;
}

void XpressV2Variable::setLB(double lb) {
  if (hasThreshold()) {
    throw SolverError(
        fmt::format(
            "Cannot set lower bound {} on variable type {}; use a threshold",
            lb,
            fmt::underlying(type_)));
  }
  problem_->markModified();
  variable_.setLB(lb);
}

void XpressV2Variable::setUB(double ub) {
  problem_->markModified();
  variable_.setUB(ub);
}

void XpressV2Variable::setThreshold(double threshold) {
  if (!hasThreshold()) {
    throw SolverError(
        fmt::format(
            "Cannot set threshold {} on variable type {}",
            threshold,
            fmt::underlying(type_)));
  }
  problem_->markModified();
  variable_.setLimit(threshold);
}

double XpressV2Variable::getValue() const {
  const int index = variable_.getIndex();
  if (solution_ == nullptr || index < 0 ||
      static_cast<size_t>(index) >= solution_->size()) {
    throw std::runtime_error(
        "XpressV2Variable: no solution available; solve the problem first");
  }
  return (*solution_)[index];
}

double XpressV2Variable::getReducedCost() const {
  if (!problem_->hasLpSensitivity()) {
    throw std::runtime_error(
        "getReducedCost() requires an optimal LP solve with the model unchanged");
  }
  return variable_.getRedCost();
}

std::shared_ptr<ExpressionImpl> XpressV2Variable::makeExpression(
    double coeff) const {
  return std::make_shared<XpressV2Expression>(variable_, coeff, solution_);
}

const xpress::objects::Variable& XpressV2Variable::get() const {
  return variable_;
}

XpressV2Expression::XpressV2Expression(
    const xpress::objects::Expression& expression,
    XpressV2SolutionSlot solution)
    : solution_{std::move(solution)} {
  // A general expression may already be quadratic, so it cannot be decomposed
  // into the linear term vectors.
  quadratic_ = expression;
}

XpressV2Expression::XpressV2Expression(
    const xpress::objects::Variable& variable,
    double coefficient,
    XpressV2SolutionSlot solution)
    : variables_{variable},
      coefficients_{coefficient},
      solution_{std::move(solution)} {}

XpressV2Expression::XpressV2Expression(
    double constant,
    XpressV2SolutionSlot solution)
    : constant_{constant}, solution_{std::move(solution)} {}

bool XpressV2Expression::isLinear() const {
  return !quadratic_.has_value();
}

bool XpressV2Expression::isPureConstant() const {
  return isLinear() && variables_.empty();
}

void XpressV2Expression::clearLinearTerms() {
  variables_.clear();
  coefficients_.clear();
  constant_ = 0.0;
}

xpress::objects::LinExpression XpressV2Expression::materialise() const {
  auto expression = xpress::objects::LinExpression::create(
      constant_, static_cast<int>(variables_.size()));
  if (!variables_.empty()) {
    expression.addTerms(
        xpress::SizedArray<const xpress::objects::Variable>{variables_},
        xpress::SizedArray<const double>{coefficients_});
  }
  return expression;
}

xpress::objects::Expression XpressV2Expression::get() const {
  return isLinear() ? xpress::objects::Expression{materialise()} : *quadratic_;
}

std::shared_ptr<ExpressionImpl> XpressV2Expression::clone() const {
  auto copy = std::make_shared<XpressV2Expression>(0.0, solution_);
  copy->variables_ = variables_;
  copy->coefficients_ = coefficients_;
  copy->constant_ = constant_;
  copy->quadratic_ = quadratic_;
  return copy;
}

void XpressV2Expression::add(double constant) {
  if (isLinear()) {
    constant_ += constant;
    return;
  }
  quadratic_ = *quadratic_ + constant;
}

void XpressV2Expression::add(std::shared_ptr<const ExpressionImpl> expression) {
  const auto& other = asV2Expression(*expression);
  if (isLinear() && other.isLinear()) {
    if (&other == this) {
      // Self-add: the source iterators would be invalidated by the insert that
      // grows the same vector, so double in place instead.
      multiply(2.0);
      return;
    }
    variables_.insert(
        variables_.end(), other.variables_.begin(), other.variables_.end());
    coefficients_.insert(
        coefficients_.end(),
        other.coefficients_.begin(),
        other.coefficients_.end());
    constant_ += other.constant_;
    return;
  }
  quadratic_ = get() + other.get();
  clearLinearTerms();
}

void XpressV2Expression::multiply(double constant) {
  if (isLinear()) {
    for (auto& coefficient : coefficients_) {
      coefficient *= constant;
    }
    constant_ *= constant;
    return;
  }
  quadratic_ = *quadratic_ * constant;
}

void XpressV2Expression::multiply(
    std::shared_ptr<const ExpressionImpl> expression) {
  const auto& other = asV2Expression(*expression);
  // Scaling by a constant is not a quadratic, whichever side the constant is
  // on, so both of those cases stay on the linear fast path.
  if (other.isPureConstant()) {
    multiply(other.constant_);
    return;
  }
  if (isPureConstant()) {
    const double scale = constant_;
    variables_ = other.variables_;
    coefficients_ = other.coefficients_;
    constant_ = other.constant_;
    quadratic_ = other.quadratic_;
    multiply(scale);
    return;
  }
  // The product of two expressions with terms is quadratic.
  if (&other == this) {
    // Self-multiply: materialise once, so squaring cannot depend on the operand
    // surviving the clearLinearTerms() below.
    const auto self = get();
    quadratic_ = self * self;
  } else {
    quadratic_ = get() * other.get();
  }
  clearLinearTerms();
}

std::shared_ptr<RelationImpl> XpressV2Expression::makeEqualZeroRelation()
    const {
  if (isLinear()) {
    return std::make_shared<XpressV2Relation>(
        materialise(), XpressV2Relation::Sense::EqualZero);
  }
  return std::make_shared<XpressV2Relation>(
      *quadratic_, XpressV2Relation::Sense::EqualZero);
}

std::shared_ptr<RelationImpl> XpressV2Expression::makeLessEqualZeroRelation()
    const {
  if (isLinear()) {
    return std::make_shared<XpressV2Relation>(
        materialise(), XpressV2Relation::Sense::LessEqualZero);
  }
  return std::make_shared<XpressV2Relation>(
      *quadratic_, XpressV2Relation::Sense::LessEqualZero);
}

std::shared_ptr<RelationImpl> XpressV2Expression::makeGreaterEqualZeroRelation()
    const {
  if (isLinear()) {
    return std::make_shared<XpressV2Relation>(
        materialise(), XpressV2Relation::Sense::GreaterEqualZero);
  }
  return std::make_shared<XpressV2Relation>(
      *quadratic_, XpressV2Relation::Sense::GreaterEqualZero);
}

double XpressV2Expression::getValue() const {
  return computeValue();
}

double XpressV2Expression::computeValue() const {
  // A constant has a value whether or not the problem has been solved.
  if (isPureConstant()) {
    return constant_;
  }
  if (solution_ == nullptr || solution_->empty()) {
    throw std::runtime_error(
        "XpressV2Expression: no solution available; the problem must be solved "
        "before an expression can be evaluated");
  }
  return get().evaluate(*solution_);
}

double XpressV2Expression::getConstant() const {
  // Once the expression goes quadratic, constant_ has already been folded into
  // quadratic_ by get() and reset to zero by clearLinearTerms(), so the live
  // value has to come back out of the Xpress expression.
  return isLinear() ? constant_ : quadratic_->getConstantView();
}

void XpressV2Expression::print() const {
  std::cout << get().toString() << '\n';
}

XpressV2Relation::XpressV2Relation(
    xpress::objects::LinExpression expression,
    Sense sense)
    : linear_{std::move(expression)}, sense_{sense} {}

XpressV2Relation::XpressV2Relation(
    xpress::objects::Expression expression,
    Sense sense)
    : general_{std::move(expression)}, sense_{sense} {}

XpressV2Relation::Sense XpressV2Relation::sense() const {
  return sense_;
}

namespace {

template <typename ExprT>
xpress::objects::InequalityDefinition makeDefinitionFrom(
    const ExprT& expression,
    XpressV2Relation::Sense sense) {
  switch (sense) {
    case XpressV2Relation::Sense::EqualZero:
      return expression == 0.0;
    case XpressV2Relation::Sense::LessEqualZero:
      return expression <= 0.0;
    case XpressV2Relation::Sense::GreaterEqualZero:
      return expression >= 0.0;
  }
  throw std::runtime_error(
      fmt::format(
          "XpressV2Relation: unhandled sense {}", static_cast<int>(sense)));
}

} // namespace

xpress::objects::InequalityDefinition XpressV2Relation::makeDefinition() const {
  if (linear_.has_value()) {
    return makeDefinitionFrom(*linear_, sense_);
  }
  return makeDefinitionFrom(*general_, sense_);
}

XpressV2Constraint::XpressV2Constraint(
    const xpress::objects::Inequality& inequality,
    const XpressProblemV2* problem)
    : inequality_{inequality}, problem_{problem} {}

double XpressV2Constraint::getDual() const {
  if (!problem_->hasLpSensitivity()) {
    throw std::runtime_error(
        "getDual() requires an optimal LP solve with the model unchanged");
  }
  return inequality_.getDual();
}

double XpressV2Constraint::getSlack() const {
  return inequality_.getSlack();
}

double XpressV2Constraint::getRHS() const {
  auto inequality = inequality_;
  return inequality.getRhs();
}

const xpress::objects::Inequality& XpressV2Constraint::get() const {
  return inequality_;
}

} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
