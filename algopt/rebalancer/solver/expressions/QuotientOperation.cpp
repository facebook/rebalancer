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

#include "algopt/rebalancer/solver/expressions/QuotientOperation.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"

#include <folly/container/irange.h>
#include <folly/logging/xlog.h>

namespace {
constexpr std::string_view type = "Quotient";
}

namespace facebook::rebalancer {

constexpr int N_SEGMENT_LOG =
    8; // Partitioning [0,1] to 2^N_SEGMENT_LOG intervals

static double safe_div(double numerator, double denominator) {
  if (std::isnan(numerator) || std::isnan(denominator)) {
    throw std::runtime_error(
        "safe_div: numerator and denominator should be valid doubles: not nan");
  }
  if (std::isinf(numerator) && std::isinf(denominator)) {
    throw std::runtime_error(
        "safe_div: numerator and denominator should be valid doubles: not inf");
  }

  return (denominator == 0)
      ? numerator * std::numeric_limits<double>::infinity() // for sign
      : (numerator / denominator);
}

QuotientOperation::QuotientOperation(
    std::shared_ptr<Expression> expr1,
    std::shared_ptr<Expression> expr2,
    std::shared_ptr<const entities::Universe> universe)
    : BinaryOperation(std::move(expr1), std::move(expr2), std::move(universe)) {
  setInitialValue(
      safe_div(child1st->getInitialValue(), child2nd->getInitialValue()));
}

const std::string_view& QuotientOperation::getType() const {
  return type;
}

double QuotientOperation::evaluate(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  return safe_div(
      evaluator.evaluate(child1st.get(), changes),
      evaluator.evaluate(child2nd.get(), changes));
}

double QuotientOperation::_applyUsingChildValues(
    const Evaluator& evaluator,
    const Assignment& assignment) {
  value = safe_div(
      evaluator.apply(child1st.get(), assignment),
      evaluator.apply(child2nd.get(), assignment));
  return value;
}

double QuotientOperation::innerFullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  return _applyUsingChildValues(evaluator, assignment);
}

double QuotientOperation::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    const ChangeSet& /*unused*/) {
  return _applyUsingChildValues(evaluator, assignment);
}

void QuotientOperation::lpIntent(
    const LpEvaluator& evaluator,
    bool /* minimizing */) {
  evaluator.computeLpIntent(child1st, false);
  evaluator.computeLpIntent(child2nd, false);
}

algopt::lp::Expression QuotientOperation::lp(
    const LpEvaluator& evaluator,
    bool /* minimizing */,
    const interface::OptimalSolverSpec& configs) {
  auto [y_lb, y_ub] = evaluator.lowerAndUpperBounds(child2nd.get());
  if (y_lb < 0 && y_ub > 0) {
    XLOG(ERR) << "Bounds for denominator contains 0!!!";
  }

  return approximate_quotient_log(
      evaluator.lp(child1st.get(), false, configs),
      evaluator.lp(child2nd.get(), false, configs),
      y_lb,
      y_ub,
      N_SEGMENT_LOG,
      evaluator);
}

/***
 * Linearization with logarithmic complexity, appears to be more stable.
 * This algorithm is described in section 4.2.2 of
 * the paper "APOGEE: Global Optimization of Standard, Generalized, and
 * Extended Pooling Problems via Linear and Logarithmic
 * Partitioning Schemes" by Misener, Thompson, and Floudas in 2010.
 ***/
algopt::lp::Expression QuotientOperation::approximate_quotient_log(
    const algopt::lp::Expression& numerator,
    const algopt::lp::Expression& denominator,
    double denom_lb,
    double denom_ub,
    int log_n_segments,
    const LpEvaluator& evaluator) {
  constexpr double q_lb = 0.0;
  constexpr double q_ub = 1.0;
  // Note: two constraints below are implicitly added:
  // REBALANCER_NEWCTR(numerator <= q_ub * denominator);
  // REBALANCER_NEWCTR(numerator >= q_lb * denominator);
  const double stride = (q_ub - q_lb) / pow(2.0, log_n_segments);
  const double denom_len = denom_ub - denom_lb;
  if (log_n_segments <= 0 || denom_len < 0) {
    throw std::runtime_error("Input error when approximating x/y");
  }

  auto quotient = evaluator.makeLpExpression();
  quotient += lp_cont_var(evaluator);
  REBALANCER_NEWCTR(quotient >= q_lb);
  REBALANCER_NEWCTR(quotient <= q_ub);

  if (denom_lb == denom_ub) {
    REBALANCER_NEWCTR(quotient == (1.0 / denom_lb) * numerator);
    return quotient;
  }

  // zeta[i] are binaries indicating intervals in partition of [q_lb, q_ub],
  // i.e., quotient is between
  // q_lb + sum(2^i * stride * zeta[i] for i=0,...,log_n_segments-1)
  // and
  // q_lb + stride + sum(2^i * stride * zeta[i] for i=0,...,log_n_segments-1)
  // w[i] represents nonlinear expression "(denominator - denom_lb) * zeta[i]"
  // slack[i] is auxiliary variable such that
  // w[i] + slack[i] = denominator - denom_lb
  std::vector<algopt::lp::Variable> zeta, w, slack;

  for ([[maybe_unused]] const auto _ : folly::irange(log_n_segments)) {
    w.push_back(lp_cont_var(evaluator));
    zeta.push_back(lp_bool_var(evaluator));
    slack.push_back(lp_cont_var(evaluator));
    REBALANCER_NEWCTR(w.back() >= 0);
    REBALANCER_NEWCTR(w.back() <= denom_len * zeta.back());
    REBALANCER_NEWCTR(slack.back() >= 0);
    REBALANCER_NEWCTR(slack.back() <= denom_len * (1 - zeta.back()));
    REBALANCER_NEWCTR(w.back() == (denominator - denom_lb) - slack.back());
  }

  // Suppose variable a is in [a_lb, a_ub], variable b is in [b_lb, b_ub], then
  // triplet (a, b a*b) satisfy the following McCormick (or RLT) inequalites:
  // a*b >= a_lb * b + q * b_lb - a_lb * b_lb
  // a*b >= a_ub * b + q * b_ub - a_ub * b_ub
  // a*b <= a_lb * b + q * b_ub - a_lb * b_ub
  // a*b <= a_ub * b + q * b_lb - a_ub * b_lb
  // The following four inequalities are extended version with
  // [a_lb, a_ub] partitioned.
  // Derivation is complicated and can be found in paper.
  auto RLT1 = evaluator.makeLpExpression();
  for (const auto i : folly::irange(log_n_segments)) {
    RLT1 += stride * pow(2.0, i) * w.at(i);
  }
  REBALANCER_NEWCTR(
      numerator >=
      quotient * denom_lb + q_lb * (denominator - denom_lb) + RLT1);

  auto RLT2 = evaluator.makeLpExpression();
  for (const auto i : folly::irange(log_n_segments)) {
    RLT2 += stride * pow(2.0, i) * (w.at(i) - denom_len * zeta.at(i));
  }
  REBALANCER_NEWCTR(
      numerator >=
      quotient * denom_ub + (q_lb + stride) * (denominator - denom_ub) + RLT2);

  // reuse RLT1
  REBALANCER_NEWCTR(
      numerator <=
      quotient * denom_lb + (q_lb + stride) * (denominator - denom_lb) + RLT1);

  // reuse RLT2
  REBALANCER_NEWCTR(
      numerator <=
      quotient * denom_ub + q_lb * (denominator - denom_ub) + RLT2);
  return quotient;
}

std::vector<double> QuotientOperation::bound_candidates(
    Context& context,
    const BoundConstraints& bc) const {
  std::vector<std::vector<double>> bounds;
  auto [child1Lb, child1Ub] = child1st->lowerAndUpperBounds(context, bc);
  auto [child2Lb, child2Ub] = child2nd->lowerAndUpperBounds(context, bc);
  bounds.push_back({child1Lb, child1Ub});
  bounds.push_back({child2Lb, child2Ub});
  std::vector<double> candidates;

  const double inf = std::numeric_limits<double>::infinity();
  // Use max to replace inf as inf * 0 returns nan
  const bool denom_contains_zero = bounds[1][0] < 0 && bounds[1][1] > 0;

  for (const auto i : folly::irange(2)) {
    for (const auto j : folly::irange(2)) {
      const double bound_candidate = safe_div(bounds[0][i], bounds[1][j]);
      if (!std::isnan(bound_candidate)) {
        candidates.push_back(bound_candidate);
      }
    }
  }
  // when numerator is not (0,0)
  if (!(bounds[0][0] == 0 && bounds[0][1] == 0) && denom_contains_zero) {
    // denominator (-x,y) , always lead to bounds (-inf, inf)
    candidates.push_back(inf);
    candidates.push_back(-inf);
  }
  return candidates;
}

ExpressionProperties QuotientOperation::getProperties() const {
  ExpressionProperties properties;
  properties.properties()->emplace(
      "type", PropertiesHelper::makeStringValue("QUOTIENT"));
  return properties;
}

} // namespace facebook::rebalancer
