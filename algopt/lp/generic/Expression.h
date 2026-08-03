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

class RelationImpl;
class VariableImpl;

class ExpressionImpl {
 public:
  virtual ~ExpressionImpl() = default;

  virtual std::shared_ptr<ExpressionImpl> clone() const = 0;

  virtual void add(double constant) = 0;
  virtual void add(std::shared_ptr<const ExpressionImpl> expression) = 0;

  virtual void multiply(double constant) = 0;
  virtual void multiply(std::shared_ptr<const ExpressionImpl> expression) = 0;

  virtual std::shared_ptr<RelationImpl> makeEqualZeroRelation() const = 0;
  virtual std::shared_ptr<RelationImpl> makeLessEqualZeroRelation() const = 0;
  virtual std::shared_ptr<RelationImpl> makeGreaterEqualZeroRelation()
      const = 0;

  virtual double getValue() const = 0;

  // computeValue() is used to recompute the value of an expression using the
  // variable values provided by the solver. When possible, the variable values
  // used in this computation are such that for integer and binary variables,
  // the values provided by the solver are rounded to ensure that they are
  // integral. This is useful in avoiding numerical errors when computing the
  // value of an expression
  virtual double computeValue() const = 0;

  // Returns the constant/offset term of the expression (the part with no
  // variables). Backends store this alongside their coefficient representation.
  virtual double getConstant() const = 0;

  virtual void print() const = 0;
};

class Variable;
class Relation;

class Expression {
 public:
  Expression();
  ~Expression() = default;

  /* implicit */ Expression(double constant);
  /* implicit */ Expression(const Variable& variable);
  explicit Expression(const std::shared_ptr<ExpressionImpl>& expression);
  Expression(const Expression& expression);
  Expression(Expression&& expression) noexcept = default;

  Expression& operator=(const Expression& expression);
  Expression& operator=(Expression&& expression) noexcept;

  std::shared_ptr<const ExpressionImpl> get() const;
  std::shared_ptr<ExpressionImpl> get();

  bool isConstant() const;
  double getConstant() const;

  void add(double constant);
  void add(const Expression& expression);

  void multiply(double constant);
  void multiply(const Expression& expression);

  Relation makeEqualZeroRelation() const;
  Relation makeLessEqualZeroRelation() const;
  Relation makeGreaterEqualZeroRelation() const;

  double getValue() const;
  double computeValue() const;

  void print() const;

 private:
  // An Expression can represent either a real constant or an ExpressionImpl
  // object. When expression_ equals nullptr, the Expression represents a
  // constant, otherwise it represents an ExpressionImpl.
  // While an ExpressionImpl can represent a real constant just fine, this
  // distinction is done as a performance optimization. We prevent an
  // unnecessary allocation of an ExpressionImpl object on the heap when the
  // Expression represents a simple constant.
  double constant_ = 0;
  std::shared_ptr<ExpressionImpl> expression_;
};

} // namespace facebook::algopt::lp
