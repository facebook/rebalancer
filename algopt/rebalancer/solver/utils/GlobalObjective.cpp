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

#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include "algopt/rebalancer/algopt_common/Precision.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>

#include <algorithm>
#include <memory>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>

namespace facebook::rebalancer {
GlobalObjective::Builder& GlobalObjective::Builder::addToObjective(
    int pos,
    ExprPtr expr,
    const entities::Universe& universe) {
  if (pos < 0) {
    throw std::runtime_error(fmt::format("pos: {} must be >= 0 ", pos));
  }
  if (built_) {
    throw std::runtime_error(
        "cannot re-use already built global objective builder");
  }

  if (pos >= static_cast<int>(objectives_.size())) {
    objectives_.resize(pos + 1, nullptr);
  }

  // protect from accidentally overwriting existing (non-null) ptr at pos
  auto cur_objective = objectives_.at(pos);
  if (cur_objective == nullptr) {
    objectives_[pos] = const_expr(0, universe);
  }

  inplace_add(objectives_.at(pos), expr);

  return *this;
}

GlobalObjective::Builder& GlobalObjective::Builder::setObjective(
    int pos,
    ExprPtr expr) {
  if (pos >= static_cast<int>(objectives_.size())) {
    objectives_.resize(pos + 1, nullptr);
  }

  objectives_.at(pos) = expr;

  return *this;
}

GlobalObjective GlobalObjective::Builder::build(
    const entities::Universe& universe) {
  if (built_) {
    throw std::runtime_error("global objective already built!");
  }

  // if no objective is specified, initialize a zero expression
  if (objectives_.size() == 0) {
    objectives_.emplace_back(const_expr(0, universe));
  }

  for (const auto pos : folly::irange(objectives_.size())) {
    if (objectives_.at(pos) == nullptr) {
      objectives_[pos] = const_expr(0, universe);
      XLOG(WARNING) << fmt::format(
          "global objective is missing data for position: {}", pos);
    }
  }
  built_ = true;
  return GlobalObjective(std::move(objectives_), universe);
}

GlobalObjective::View::View(
    const GlobalObjective& objective,
    int start,
    int end)
    : objective_(objective), start_(start), end_(end) {}

GlobalObjective::objectives_tuple::const_iterator GlobalObjective::View::begin()
    const {
  auto it_ = objective_.begin();
  for (int pos = 0; pos < start_ && it_ != objective_.end(); ++pos) {
    ++it_;
  }
  return it_;
}

GlobalObjective::objectives_tuple::const_iterator GlobalObjective::View::end()
    const {
  auto it = objective_.begin();
  for (int pos = 0; pos < end_ && it != objective_.end(); ++pos) {
    ++it;
  }
  return it;
}

int GlobalObjective::View::size() const {
  return std::min(objective_.size(), end_ - start_);
}

GlobalObjective::objectives_tuple::const_iterator GlobalObjective::begin()
    const {
  return objectives_.begin();
}

GlobalObjective::objectives_tuple::const_iterator GlobalObjective::end() const {
  return objectives_.end();
}

ExprPtr GlobalObjective::getObjectiveAt(int pos) const {
  if (pos >= static_cast<int>(objectives_.size())) {
    return nullptr;
  }
  return objectives_.at(pos);
}

ExprPtr GlobalObjective::getFirstObjective() const {
  return getObjectiveAt(0);
}

ExprPtr GlobalObjective::getOnlyObjective() const {
  if (objectives_.size() != 1) {
    throw std::runtime_error("exactly one objective expected");
  }
  return getObjectiveAt(0);
}

int GlobalObjective::size() const {
  return objectives_.size();
}

void GlobalObjective::initUnconstrainedBounds() {
  Context context;
  for (const auto& objective : objectives_) {
    context.clear();
    objective->init_unconstrained_bounds(context);
  }
}

GlobalObjectiveValue GlobalObjective::lowerBound(
    const BoundConstraints& bc) const {
  GlobalObjectiveValue value;
  Context context;
  for (const auto& objective : objectives_) {
    context.clear();
    value.append(objective->lowerAndUpperBounds(context, bc).lower_bound);
  }
  return value;
}

GlobalObjectiveValue GlobalObjective::getValue() const {
  GlobalObjectiveValue value;
  for (const auto& objective : objectives_) {
    value.append(objective->value);
  }
  return value;
}

double GlobalObjective::getValueAt(int pos) const {
  return objectives_.at(pos)->value;
}

GlobalObjective::View GlobalObjective::getView() const {
  return View(*this, 0, size());
}

GlobalObjective::View GlobalObjective::getView(int start, int end) const {
  return View(*this, std::max(0, start), std::min(end, size()));
}

GlobalObjective GlobalObjective::getRange(int start, int end) const {
  start = std::max(0, start);
  end = std::min(end, size());
  GlobalObjective::Builder builder;
  for (int pos = start; pos < end; ++pos) {
    builder.setObjective(pos - start, objectives_.at(pos));
  }
  return builder.build(*universe_);
}

double GlobalObjective::evaluateObjectiveAt(
    int i,
    Context& context,
    const Orchestrator& orchestrator) const {
  return orchestrator.evaluate(objectives_.at(i).get(), context);
}

GlobalObjectiveValue GlobalObjective::evaluate(
    Context& context,
    const Orchestrator& orchestrator,
    bool evaluateAllTuples) const {
  auto value = GlobalObjectiveValue::makeWithFixedSize(size());
  bool priorObjectiveImproved = false;
  for (const auto& objective : objectives_) {
    const double val = orchestrator.evaluate(objective.get(), context);
    value.append(val);

    if (evaluateAllTuples) {
      // unless evaluateAllTuples is explicitly set to 'true', we do not
      // evaluate objective i and after if objective (i-1) gets worse;
      continue;
    }

    // NOTE: we do not use Precision compare since we aggregate with default
    // double compare in MoveResult

    // If any objective improved we need to evaluate all objectives after it
    // ex: (2,2,3) => (1,3,2) vs (1,2,3) => (1,3,3) needs to be distinguished.
    // Though we see objective 2 gets worse we cannot skip evaluation of
    // objective 3, since both are improvements and we need to be able to find
    // the best among both so need values for all subsequent objectives.
    // However if no prior objective improves and something becomes worse, we
    // clearly do not care about 'all' moves that make objective worse and can
    // skip evaluation of further objectives in the GlobalObjective tuple.
    if (val < objective->value) {
      priorObjectiveImproved = true;
    } else if (
        !priorObjectiveImproved &&
        algopt::Precision::compare(val, objective->value) > 0) {
      // if a higher pri objective already gets worse and evaluateAllTuples =
      // false (which is the default option), then no need to evaluate
      // objectives that follow it
      return value;
    }
  }
  return value;
}

GlobalObjectiveValue GlobalObjective::fullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) const {
  GlobalObjectiveValue value;
  for (const auto& objective : objectives_) {
    value.append(objective->fullApply(evaluator, assignment));
  }
  return value;
}
} // namespace facebook::rebalancer
