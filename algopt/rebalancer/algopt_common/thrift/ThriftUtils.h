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

#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"

#include <folly/MapUtil.h>

#include <cmath>
#include <stdexcept>

namespace facebook::algopt::common::thriftUtils {

inline double getAllowedWorsenUntilValue(
    double currValue,
    const algopt::common::thrift::AllowedWorsening& allowedWorsen) {
  auto allowWorsenUntilRelValue =
      currValue * (1 + *allowedWorsen.percent() / 100.0);
  auto allowWorsenUntilAbsValue = currValue + *allowedWorsen.absolute();

  switch (*allowedWorsen.intent()) {
    case algopt::common::thrift::Intent::MAX:
      return std::max(allowWorsenUntilRelValue, allowWorsenUntilAbsValue);
  }

  throw std::runtime_error("Unsupported deviation intent");
}

inline double getObjectiveValue(
    int64_t index,
    const algopt::common::thrift::PerObjectiveValue& perObjectiveValue) {
  const auto& objIdxToValue = *perObjectiveValue.objectiveIndexToValue();
  return folly::get_default(
      objIdxToValue, index, *perObjectiveValue.defaultValue());
}

// Returns true if the decrease from `before` to `after` is significant,
// i.e., exceeds the percent or absolute threshold.
inline bool isSignificantDecrease(
    const algopt::common::thrift::Threshold& threshold,
    double before,
    double after) {
  if (after >= before) {
    return false;
  }

  auto pct = threshold.percent();
  auto abs = threshold.absolute();
  if (!pct.has_value() && !abs.has_value()) [[unlikely]] {
    throw std::runtime_error(
        "Threshold must have at least one of percent or absolute set");
  }

  const double decrease = before - after;

  // Percent is undefined at zero baseline; treat any decrease as significant
  if (pct.has_value() && before == 0.0) {
    return true;
  }

  const double decreasePercent = (decrease / std::abs(before)) * 100.0;
  if (pct.has_value() && decreasePercent > *pct) {
    return true;
  }

  if (abs.has_value() && decrease > *abs) {
    return true;
  }

  return false;
}

} // namespace facebook::algopt::common::thriftUtils
