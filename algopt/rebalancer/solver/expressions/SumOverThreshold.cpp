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

#include "algopt/rebalancer/solver/expressions/SumOverThreshold.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/iterators/ObjectPotentialsMerge.h"
#include "algopt/rebalancer/solver/iterators/Transform.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"

#include <folly/container/Enumerate.h>

#include <sstream>

using namespace std;

namespace {
constexpr std::string_view type = "SumOverThreshold";
}

namespace facebook::rebalancer {

SumOverThreshold::SumOverThreshold(
    std::shared_ptr<Expression> threshold,
    const std::vector<std::shared_ptr<Expression>>& values,
    bool square,
    std::shared_ptr<const entities::Universe> universe)
    : Expression(std::move(universe)),
      squares(square),
      collection(
          [](Node n1, Node n2) {
            return Node{
                .sum = n1.sum + n2.sum,
                .sum_squares = n1.sum_squares + n2.sum_squares,
                .count = n1.count + n2.count};
          },
          Node{.sum = 0.0, .sum_squares = 0.0, .count = 0}) {
  threshold_expr = threshold;
  add_child(std::move(threshold));
  for (auto& node : values) {
    add_child(node);
  }

  setInitialValue(
      populateAndEvaluate([](Expression* c) { return c->getInitialValue(); }));
}

const std::string_view& SumOverThreshold::getType() const {
  return type;
}

void SumOverThreshold::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets) const {
  PackerMap<entities::ObjectId, int> mapping1;
  PackerMap<entities::ObjectId, vector<entities::ContainerId>> mapping2;
  if (auto var = threshold_expr->getVar()) {
    auto [obj, cont] = *var;
    mapping1[obj] = 1;
  }
  for (const auto& child : children()) {
    if (auto var = child->getVar(); var && child != threshold_expr) {
      auto [obj, cont] = *var;
      mapping2[obj].push_back(cont);
    }
  }
  equivalenceSets.mappingMerge(mapping1);
  equivalenceSets.mappingMerge(mapping2);
}

double SumOverThreshold::evaluate(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  PackerMap<Expression*, double> new_values;
  auto threshold = threshold_expr->value;
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  for (auto child : changedChildren) {
    const double val = evaluator.evaluate(child, changes);
    if (child == threshold_expr.get()) {
      threshold = val;
    } else {
      new_values[child] = val;
    }
  }

  const bool threshold_changed = threshold != threshold_expr->value;
  double result = threshold_changed ? evaluate_collection(threshold) : value;
  for (auto& p : new_values) {
    auto child = p.first;
    const double old_value = values.at(child);
    const double new_value = p.second;
    result -= evaluate_single(old_value, threshold);
    result += evaluate_single(new_value, threshold);
  }

  return snapToZero(result);
}

double SumOverThreshold::innerFullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  value = populateAndEvaluate(
      [&](Expression* c) { return evaluator.apply(c, assignment); });
  return value;
}

double SumOverThreshold::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    [[maybe_unused]] const ChangeSet& changes) {
  double threshold = threshold_expr->value;
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  for (auto child : changedChildren) {
    const double new_value = evaluator.apply(child, assignment);
    if (threshold_expr.get() == child) {
      threshold = new_value;
    } else {
      const double old_value = values[child];
      if (old_value != new_value) {
        collection.remove(make_pair(old_value, child));
        collection.update(
            make_pair(new_value, child),
            Node{
                .sum = new_value,
                .sum_squares = new_value * new_value,
                .count = 1});
        values[child] = new_value;
      }
    }
  }
  value = snapToZero(evaluate_collection(threshold));
  return value;
}

Bounds SumOverThreshold::innerLowerAndUpperBounds(
    Context& context,
    const BoundConstraints& bc) const {
  auto [min_threshold, max_threshold] =
      threshold_expr->lowerAndUpperBounds(context, bc);

  double lb = 0;
  double ub = 0;
  for (const auto& child : children()) {
    if (child == threshold_expr) {
      continue;
    }
    auto [min_value, max_value] = child->lowerAndUpperBounds(context, bc);
    lb += evaluate_single(min_value, max_threshold);
    ub += evaluate_single(max_value, min_threshold);
  }
  return {.lower_bound = snapToZero(lb), .upper_bound = snapToZero(ub)};
}

void SumOverThreshold::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  evaluator.computeLpIntent(threshold_expr, minimizing);
  for (const auto& child : children()) {
    if (child == threshold_expr) {
      continue;
    }
    evaluator.computeLpIntent(child, minimizing);
  }
}

algopt::lp::Expression SumOverThreshold::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (squares) {
    throw std::runtime_error(
        "SumOverThreshold with squares is not supported yet when using lp()");
  }

  auto expr = evaluator.makeLpExpression();
  auto& threshold =
      evaluator.lp(threshold_expr.get(), /*minimizing=*/false, configs);
  auto [thresholdLb, thresholdUb] =
      evaluator.lowerAndUpperBounds(threshold_expr.get());
  for (const auto& child : children()) {
    if (child == threshold_expr) {
      continue;
    }

    auto& x = evaluator.lp(child.get(), minimizing, configs);
    auto y = lp_cont_var(evaluator);
    REBALANCER_NEWCTR(0 <= y);
    REBALANCER_NEWCTR(x - threshold <= y);
    expr += y;

    if (!minimizing) {
      auto [childLb, childUb] = evaluator.lowerAndUpperBounds(child.get());
      const double ub = max(0.0, childUb - thresholdLb);
      const double lb = min(0.0, childLb - thresholdUb);
      auto z = lp_bool_var(evaluator);
      REBALANCER_NEWCTR(y <= z * ub);
      REBALANCER_NEWCTR(y <= x - threshold + (1 - z) * -lb);
    }
  }
  return expr;
}

double SumOverThreshold::getChildCoefficient(Expression* child) const {
  return child == threshold_expr.get() ? -1 : 1;
}

AbstractContainer<ObjectPotential> SumOverThreshold::getObjectPotentials(
    bool descending) const {
  std::vector<AbstractContainer<ObjectPotential>> potentials;
  const double threshold = threshold_expr->value;
  for (auto& [childExpr, childValue] : values) {
    const double currentPenalty = evaluate_single(childValue, threshold);
    if (currentPenalty == 0) {
      continue;
    }
    const auto transform =
        [this, currentPenalty, threshold, value = childValue](
            ObjectPotential objectPotential) {
          const double newPenalty =
              evaluate_single(value - objectPotential.potential, threshold);
          return ObjectPotential{
              .objectId = objectPotential.objectId,
              .potential = currentPenalty - newPenalty};
        };
    potentials.push_back(makeTransformContainer(
        childExpr->getObjectPotentials(descending), transform));
  }
  return makeObjectPotentialsMergeContainer(potentials, descending);
}

double SumOverThreshold::evaluate_collection(double threshold) const {
  auto all = collection.query();
  auto lower = collection.query_le(make_pair(threshold, nullptr));
  auto upper = Node{
      .sum = all.sum - lower.sum,
      .sum_squares = all.sum_squares - lower.sum_squares,
      .count = all.count - lower.count};
  if (squares) {
    // we want to compute: sum((x_i - t) ^ 2)
    // which is equivalent to: sum(x_i ^ 2 + t ^ 2 - 2 * x_i * t)
    // which is equivalent to: sum(x_i ^ 2) + t ^ 2 * sum(1) - 2 * t * sum(x_i)
    return upper.sum_squares + threshold * threshold * upper.count -
        2 * threshold * upper.sum;
  }
  // we want to compute: sum(x_i - t)
  // which is equivalent to: sum(x_i) - t * sum(1)
  return upper.sum - threshold * upper.count;
}

double SumOverThreshold::evaluate_single(double value, double threshold) const {
  double result = max(0.0, value - threshold);
  if (squares) {
    result = result * result;
  }
  return result;
}

template <typename ValueFn>
double SumOverThreshold::populateAndEvaluate(ValueFn&& valueFn) {
  values.clear();
  collection.clear();
  double threshold = 0;
  for (const auto& child : children()) {
    const auto val = valueFn(child.get());
    if (threshold_expr == child) {
      threshold = val;
      continue;
    }
    values[child.get()] = val;
    collection.update(
        make_pair(val, child.get()),
        Node{.sum = val, .sum_squares = val * val, .count = 1});
  }
  return snapToZero(evaluate_collection(threshold));
}

std::string SumOverThreshold::innerDigest(size_t maxChildren) const {
  std::stringstream ss;
  ss << fmt::format(
      "threshold{} = {}, over ",
      threshold_expr->getType(),
      threshold_expr->value);
  size_t count = 0;
  const auto& children = this->children();
  for (const auto& child : children) {
    if (child == threshold_expr) {
      continue;
    }
    if (count >= maxChildren) {
      ss << "... " << children.size() - 1 - maxChildren << " more children";
      break;
    }

    if (count > 0) {
      ss << ", ";
    }
    ss << fmt::format("{}({})", child->getType(), child->value);
    count++;
  }
  ss << ")";
  return ss.str();
}

} // namespace facebook::rebalancer
