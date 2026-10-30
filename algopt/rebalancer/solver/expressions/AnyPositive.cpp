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

#include "algopt/rebalancer/solver/expressions/AnyPositive.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <folly/container/Enumerate.h>

#include <sstream>

using namespace std;

namespace {
constexpr std::string_view type = "AnyPositive";
}

namespace facebook::rebalancer {

AnyPositive::AnyPositive(
    const vector<ExprPtr>& exprs,
    const entities::Universe& universe)
    : Expression(universe) {
  for (const auto& expr : exprs) {
    add_child(expr);
    if (isViolating(expr->getInitialValue())) {
      violatingChildren_.insert(expr.get());
    }
  }
  setInitialValue(computeResult());
}

bool AnyPositive::isViolating(double childVal) const {
  return getPrecision().isStrictlyGtZero(childVal);
}

double AnyPositive::computeResult() const {
  return violatingChildren_.empty() ? 0.0 : 1.0;
}

const std::string_view& AnyPositive::getType() const {
  return type;
}

bool AnyPositive::isAnyPositive() const {
  return true;
}

void AnyPositive::add(const ExprPtr& expr) {
  if (!children().contains(expr)) {
    add_child(expr);
    if (isViolating(expr->getInitialValue())) {
      violatingChildren_.insert(expr.get());
    }
    setInitialValue(computeResult());
  }
}

void AnyPositive::combine(const ExprPtr& expr) {
  if (expr->isAnyPositive()) {
    for (auto& child : expr->children()) {
      combine(child);
    }
  } else {
    add(expr);
  }
}

void AnyPositive::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets) const {
  // similar logic to Max expression
  PackerMap<entities::ObjectId, vector<entities::ContainerId>> mapping;
  for (const auto& child : children()) {
    if (auto var = child->getVar()) {
      auto [obj, cont] = *var;
      mapping[obj].push_back(cont);
    }
  }
  for (auto& [_, containers] : mapping) {
    sort(containers.begin(), containers.end());
  }
  equivalenceSets.mappingMerge(mapping);
}

double AnyPositive::evaluate(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  // check if anything that changed fails predicate check
  size_t nonViolatingChildren = 0;
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  for (auto& child : changedChildren) {
    if (evaluator.isPositive(child, changes)) {
      return 1;
    }
    nonViolatingChildren += violatingChildren_.count(child);
  }
  return violatingChildren_.size() == nonViolatingChildren ? 0 : 1;
}

double AnyPositive::innerFullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  violatingChildren_.clear();
  for (const auto& child : children()) {
    if (isViolating(evaluator.apply(child.get(), assignment))) {
      violatingChildren_.insert(child.get());
    }
  }
  value = computeResult();
  return value;
}

double AnyPositive::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    [[maybe_unused]] const ChangeSet& changes) {
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  for (auto& child : changedChildren) {
    if (isViolating(evaluator.apply(child, assignment))) {
      violatingChildren_.insert(child);
    } else {
      violatingChildren_.erase(child);
    }
  }
  value = computeResult();
  return value;
}

Bounds AnyPositive::innerLowerAndUpperBounds(
    Context& /* unused */,
    const BoundConstraints& /* unused */) const {
  return {.lower_bound = 0.0, .upper_bound = children().empty() ? 0.0 : 1.0};
}

string AnyPositive::innerDigest(size_t maxChildren) const {
  stringstream ss;
  ss << "= ANY_POSITIVE(";
  for (const auto& [idx, child] : folly::enumerate(children())) {
    if (idx >= maxChildren) {
      ss << "... " << children().size() - maxChildren << " more";
      break;
    }
    if (idx > 0) {
      ss << ", ";
    }
    ss << fmt::format("{}({})", child->getType(), child->value);
  }
  ss << ")";
  return ss.str();
}

static ExprPtr getEquivStepExpr(
    const PackerSet<ExprPtr>& children,
    const entities::Universe& universe) {
  std::vector<ExprPtr> exprs;
  exprs.reserve(children.size());
  for (auto& child : children) {
    exprs.push_back(child);
  }
  return step(max(exprs, universe));
}

void AnyPositive::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  if (!lpProvider_) {
    lpProvider_ = getEquivStepExpr(children(), getUniverse());
  }
  return evaluator.computeLpIntent(lpProvider_, minimizing);
}

algopt::lp::Expression AnyPositive::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (children().size() == 0) {
    auto def = lp_bool_var(evaluator);
    evaluator.addLpConstraint(def == 0, "any_positive_default");
    return evaluator.makeLpExpression(0);
  }

  if (!lpProvider_) {
    lpProvider_ = getEquivStepExpr(children(), getUniverse());
  }
  return evaluator.lp(lpProvider_.get(), minimizing, configs);
}

bool AnyPositive::inner_is_integer(Context& /* not used */) {
  return true;
}

} // namespace facebook::rebalancer
