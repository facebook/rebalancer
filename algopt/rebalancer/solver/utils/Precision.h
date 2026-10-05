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

#include "algopt/rebalancer/algopt_common/Precision.h"
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"

#include <utility>

namespace facebook::rebalancer {

class Precision {
 public:
  explicit Precision(algopt::common::thrift::PrecisionTolerances tolerances)
      : tolerances_(std::move(tolerances)) {}

  int compare(double val1, double val2) const {
    return algopt::PrecisionImpl::compare(val1, val2, tolerances_);
  }

  // Returns true if value is an integer
  bool isInteger(double value) const {
    return algopt::PrecisionImpl::isInteger(value, tolerances_);
  }

  // Returns true if value is either 0 or 1
  bool isZeroOrOne(double value) const {
    return algopt::PrecisionImpl::isZeroOrOne(value, tolerances_);
  }

  bool isZero(double value) const {
    return algopt::PrecisionImpl::isZero(value, tolerances_);
  }

  // Returns true if value is strictly greater than 0
  bool isStrictlyGtZero(double val) const {
    return algopt::PrecisionImpl::isStrictlyGtZero(val, tolerances_);
  }

  // Returns true if val > val2 (syntax sugar for compare(val1, val2) == 1)
  bool isstrictlyGreater(double val1, double val2) const {
    return algopt::PrecisionImpl::isstrictlyGreater(val1, val2, tolerances_);
  }

  // Returns true if val1 == val2
  bool isEqual(double val1, double val2) const {
    return algopt::PrecisionImpl::isEqual(val1, val2, tolerances_);
  }

  // Returns true if val >= val2
  bool isGreaterOrEqual(double val1, double val2) const {
    return algopt::PrecisionImpl::isGreaterOrEqual(val1, val2, tolerances_);
  }

  // Returns true if val < val2
  bool isStrictlyLesser(double val1, double val2) const {
    return algopt::PrecisionImpl::isStrictlyLesser(val1, val2, tolerances_);
  }

  // Returns true if val <= val2
  bool isLesserOrEqual(double val1, double val2) const {
    return algopt::PrecisionImpl::isLesserOrEqual(val1, val2, tolerances_);
  }

  algopt::common::thrift::PrecisionTolerances getTolerances() const {
    return tolerances_;
  }

 private:
  algopt::common::thrift::PrecisionTolerances tolerances_;
};

} // namespace facebook::rebalancer
