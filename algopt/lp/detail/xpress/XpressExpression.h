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

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/xpress.h"
#include "algopt/lp/generic/Expression.h"

namespace facebook::algopt::lp::detail {

class XpressExpression : public ExpressionImpl {
 public:
  // Constructed by the XpressProblem / XpressVariable factories and clone().
  // There is deliberately no default for `constant`: every caller must pass the
  // constant that `expression` was built with, so the shadow constant_ never
  // drifts from the wrapped XPRBexpr. See constant_ below for why it exists.
  XpressExpression(
      const dashoptimization::XPRBexpr& expression,
      double constant);

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

  const dashoptimization::XPRBexpr& get() const;

 private:
  static constexpr int kMaxBufferSize = 1000;

 private:
  template <typename T>
  void addBuffered(const T& term) {
    buffer_ += term;
    ++bufferSize_;

    if (bufferSize_ >= kMaxBufferSize) {
      flushBuffer();
    }
  }

  void flushBuffer() const;

 private:
  mutable dashoptimization::XPRBexpr expression_;
  mutable dashoptimization::XPRBexpr buffer_;
  mutable int bufferSize_ = 0;
  // Shadow copy of the expression's constant/offset term.
  //
  // The FICO XPRBexpr API is write-only for the constant: you can set it (via
  // setTerm/addTerm with a null variable) but there is no getter. The only way
  // to read a value back, XPRBevalexpr (getSol), evaluates the whole expression
  // using the variables' current solution values -- so it is unusable at build
  // time and it mixes in the variable terms rather than isolating the constant.
  // getConstant() must return just the constant without a solve, so we maintain
  // this shadow in lockstep with the wrapped XPRBexpr: the constructor takes
  // the initial constant, add()/multiply() update it, and clone() copies it.
  double constant_;
};

} // namespace facebook::algopt::lp::detail

#endif
