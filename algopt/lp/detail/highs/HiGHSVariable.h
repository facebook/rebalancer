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

#include "algopt/lp/detail/highs/highs.h"
#include "algopt/lp/generic/Variable.h"

#include <string>

namespace facebook::algopt::lp::detail {

class HiGHSVariable : public VariableImpl {
 public:
  HiGHSVariable(
      Highs* highs,
      HighsInt colIndex,
      const std::string& name,
      Type type);

  /// Set the variable's lower bound
  void setLB(double lb) override;

  /// Set the variable's upper bound
  void setUB(double ub) override;
  void setThreshold(double threshold) override;

  /// Get the value of the variable in the best solution
  double getValue() const override;

  /// Get the reduced cost of the variable in the solved LP
  double getReducedCost() const override;

  /// Get the name of the variable
  const std::string& getName() const;

  /// Create an expression representing coeff * variable
  std::shared_ptr<ExpressionImpl> makeExpression(double coeff) const override;

  /// Get the underlying column index
  HighsInt getColIndex() const;

  /// Get a pointer to the Highs instance
  Highs* getHighs() const;

 private:
  Highs* highs_; // Non-owning pointer
  HighsInt colIndex_;
  std::string name_;
};

} // namespace facebook::algopt::lp::detail

#endif
