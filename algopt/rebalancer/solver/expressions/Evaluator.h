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

#include "algopt/rebalancer/solver/expressions/Expression.h"

namespace facebook::rebalancer {

class Evaluator {
 public:
  virtual ~Evaluator();

  virtual double evaluate(const Expression* expr, const ChangeSet& changes)
      const = 0;

  virtual double apply(Expression* expr, const Assignment& assignment)
      const = 0;

  virtual const folly::F14FastSet<Expression*>& getChangedChildren(
      const Expression* const expr) const = 0;

  virtual bool isPositive(const Expression* expr, const ChangeSet& changes)
      const = 0;

  virtual Context& getContext() const = 0;

  Bounds lowerAndUpperBounds(
      Expression* expr,
      const BoundConstraints& bc = BoundConstraints()) const;

  bool isBinary(Expression* expr) const;
  bool isInteger(Expression* expr) const;
};
} // namespace facebook::rebalancer
