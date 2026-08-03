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

#include "algopt/lp/generic/Expression.h"

#include "algopt/lp/generic/Relation.h"
#include "algopt/lp/generic/Variable.h"

#include <iostream>

namespace facebook::algopt::lp {

Expression::Expression() : constant_(0) {}

Expression::Expression(double constant) : constant_(constant) {}

Expression::Expression(const Variable& variable)
    : Expression(variable.get()->makeExpression(1.0)) {}

Expression::Expression(const std::shared_ptr<ExpressionImpl>& expression)
    : expression_(expression) {}

Expression::Expression(const Expression& expression)
    : constant_(expression.constant_),
      expression_(
          expression.expression_ ? expression.expression_->clone() : nullptr) {}

Expression& Expression::operator=(const Expression& expression) {
  if (expression.expression_) {
    expression_ = expression.expression_->clone();
  } else {
    constant_ = expression.constant_;
    expression_ = nullptr;
  }
  return *this;
}

Expression& Expression::operator=(Expression&& expression) noexcept {
  if (expression.expression_) {
    expression_ = expression.expression_;
  } else {
    constant_ = expression.constant_;
    expression_ = nullptr;
  }
  return *this;
}

std::shared_ptr<const ExpressionImpl> Expression::get() const {
  return expression_;
}

std::shared_ptr<ExpressionImpl> Expression::get() {
  return expression_;
}

bool Expression::isConstant() const {
  return !expression_;
}

double Expression::getConstant() const {
  return expression_ ? expression_->getConstant() : constant_;
}

void Expression::add(double constant) {
  if (expression_) {
    expression_->add(constant);
  } else {
    constant_ += constant;
  }
}

void Expression::add(const Expression& expression) {
  if (expression_) {
    if (expression.expression_) {
      expression_->add(expression.expression_);
    } else {
      expression_->add(expression.constant_);
    }
  } else {
    if (expression.expression_) {
      expression_ = expression.expression_->clone();
      expression_->add(constant_);
    } else {
      constant_ += expression.constant_;
    }
  }
}

void Expression::multiply(double constant) {
  if (expression_) {
    expression_->multiply(constant);
  } else {
    constant_ *= constant;
  }
}

void Expression::multiply(const Expression& expression) {
  if (expression.expression_) {
    if (expression_) {
      expression_->multiply(expression.expression_);
    } else {
      expression_ = expression.expression_->clone();
      expression_->multiply(constant_);
    }
  } else {
    if (expression_) {
      expression_->multiply(expression.constant_);
    } else {
      constant_ *= expression.constant_;
    }
  }
}

Relation Expression::makeEqualZeroRelation() const {
  return expression_ ? Relation(expression_->makeEqualZeroRelation())
                     : Relation(constant_, Relation::ConstantType::EQ_ZERO);
}

Relation Expression::makeLessEqualZeroRelation() const {
  return expression_ ? Relation(expression_->makeLessEqualZeroRelation())
                     : Relation(constant_, Relation::ConstantType::LE_ZERO);
}

Relation Expression::makeGreaterEqualZeroRelation() const {
  return expression_ ? Relation(expression_->makeGreaterEqualZeroRelation())
                     : Relation(constant_, Relation::ConstantType::GE_ZERO);
}

double Expression::getValue() const {
  return expression_ ? expression_->getValue() : constant_;
}

double Expression::computeValue() const {
  return expression_ ? expression_->computeValue() : constant_;
}

void Expression::print() const {
  if (expression_) {
    expression_->print();
  } else {
    std::cout << constant_ << std::endl;
  }
}

} // namespace facebook::algopt::lp
