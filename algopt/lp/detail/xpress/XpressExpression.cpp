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

#include "algopt/lp/detail/xpress/XpressExpression.h"

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/XpressRelation.h"

#include <folly/logging/xlog.h>

namespace facebook::algopt::lp::detail {

XpressExpression::XpressExpression(
    const dashoptimization::XPRBexpr& expression,
    double constant)
    : expression_(expression), constant_(constant) {}

std::shared_ptr<ExpressionImpl> XpressExpression::clone() const {
  flushBuffer();
  return std::make_shared<XpressExpression>(expression_, constant_);
}

void XpressExpression::add(double constant) {
  constant_ += constant;
  addBuffered(constant);
}

void XpressExpression::add(std::shared_ptr<const ExpressionImpl> expression) {
  const auto& other = dynamic_cast<const XpressExpression&>(*expression);
  constant_ += other.getConstant();
  addBuffered(other.get());
}

void XpressExpression::multiply(double constant) {
  flushBuffer();
  constant_ *= constant;
  expression_.mul(constant);
}

void XpressExpression::multiply(
    std::shared_ptr<const ExpressionImpl> expression) {
  flushBuffer();
  const auto& other = dynamic_cast<const XpressExpression&>(*expression);
  constant_ *= other.getConstant();
  expression_.mul(other.get());
}

std::shared_ptr<RelationImpl> XpressExpression::makeEqualZeroRelation() const {
  flushBuffer();
  return std::make_shared<XpressRelation>(
      dashoptimization::XPRBrelation(expression_, XB_E));
}

std::shared_ptr<RelationImpl> XpressExpression::makeLessEqualZeroRelation()
    const {
  flushBuffer();
  return std::make_shared<XpressRelation>(
      dashoptimization::XPRBrelation(expression_, XB_L));
}

std::shared_ptr<RelationImpl> XpressExpression::makeGreaterEqualZeroRelation()
    const {
  flushBuffer();
  return std::make_shared<XpressRelation>(
      dashoptimization::XPRBrelation(expression_, XB_G));
}

double XpressExpression::getValue() const {
  flushBuffer();
  return expression_.getSol();
}

double XpressExpression::computeValue() const {
  // for now, this returns the same value as getValue, since Xpress does not
  // seem to provide a way to access the underlying variables used in an
  // expression
  return getValue();
}

double XpressExpression::getConstant() const {
  return constant_;
}

void XpressExpression::print() const {
  flushBuffer();
  expression_.print();
}

const dashoptimization::XPRBexpr& XpressExpression::get() const {
  flushBuffer();
  return expression_;
}

void XpressExpression::flushBuffer() const {
  if (bufferSize_ != 0) {
    expression_ += buffer_;
    buffer_ = 0;
    bufferSize_ = 0;
  }
}

} // namespace facebook::algopt::lp::detail

#endif
