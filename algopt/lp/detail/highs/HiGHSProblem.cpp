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

#ifdef REBALANCER_USE_HIGHS

#include "algopt/lp/detail/highs/HiGHSProblem.h"

#include "algopt/lp/detail/highs/highs.h"
#include "algopt/lp/detail/highs/HiGHSConstraint.h"
#include "algopt/lp/detail/highs/HiGHSExpression.h"
#include "algopt/lp/detail/highs/HiGHSRelation.h"
#include "algopt/lp/detail/highs/HiGHSVariable.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/logging/xlog.h>
#include <folly/String.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace facebook::algopt::lp::detail {

HiGHSProblem::HiGHSProblem() {
  // HiGHS defaults to minimization, which is what we want
  // Suppress output by default (users can enable via setLogfile or by not
  // calling disableLogs)
  REBALANCER_HIGHS_CALL(highs_.setOptionValue("output_flag", false));
}

HiGHSProblem::~HiGHSProblem() = default;

std::shared_ptr<VariableImpl> HiGHSProblem::makeVar(const std::string& name) {
  return makeVar(name, VariableImpl::Type::CONTINUOUS);
}

std::shared_ptr<VariableImpl> HiGHSProblem::makeIntVar(
    const std::string& name) {
  return makeVar(name, VariableImpl::Type::INTEGER);
}

std::shared_ptr<VariableImpl> HiGHSProblem::makeSemiContVar(
    const std::string& name,
    double threshold) {
  auto var = makeVar(
      name, VariableImpl::Type::SEMI_CONTINUOUS, threshold, std::nullopt);
  return var;
}

std::shared_ptr<VariableImpl> HiGHSProblem::makeSemiIntVar(
    const std::string& name,
    double threshold) {
  auto var =
      makeVar(name, VariableImpl::Type::SEMI_INTEGER, threshold, std::nullopt);
  return var;
}

std::shared_ptr<VariableImpl> HiGHSProblem::makeBoolVar(
    const std::string& name) {
  return makeVar(name, VariableImpl::Type::BINARY, 0, 1);
}

std::shared_ptr<ExpressionImpl> HiGHSProblem::makeExpression(
    double constant) const {
  return std::make_shared<HiGHSExpression>(constant);
}

std::shared_ptr<VariableImpl> HiGHSProblem::makeVar(
    const std::string& name,
    VariableImpl::Type type,
    std::optional<double> lb,
    std::optional<double> ub) {
  const double lowerBound = lb.value_or(-kHighsInf);
  const double upperBound = ub.value_or(kHighsInf);

  // Add a column (variable) to the model
  REBALANCER_HIGHS_CALL(highs_.addVar(lowerBound, upperBound));

  const HighsInt colIndex = highs_.getNumCol() - 1;
  colNames_.push_back(name);

  // Set the variable type if not continuous
  switch (type) {
    case VariableImpl::Type::CONTINUOUS:
      // Default type, no need to change
      break;
    case VariableImpl::Type::INTEGER:
      REBALANCER_HIGHS_CALL(
          highs_.changeColIntegrality(colIndex, HighsVarType::kInteger));
      break;
    case VariableImpl::Type::BINARY:
      REBALANCER_HIGHS_CALL(
          highs_.changeColIntegrality(colIndex, HighsVarType::kInteger));
      break;
    case VariableImpl::Type::SEMI_CONTINUOUS:
      REBALANCER_HIGHS_CALL(
          highs_.changeColIntegrality(colIndex, HighsVarType::kSemiContinuous));
      break;
    case VariableImpl::Type::SEMI_INTEGER:
      REBALANCER_HIGHS_CALL(
          highs_.changeColIntegrality(colIndex, HighsVarType::kSemiInteger));
      break;
    default:
      throw std::runtime_error(
          fmt::format("unknown variable type {}", static_cast<int>(type)));
  }

  return std::make_shared<HiGHSVariable>(&highs_, colIndex, name, type);
}

std::shared_ptr<ConstraintImpl> HiGHSProblem::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) {
  const auto& highsRelation = dynamic_cast<const HiGHSRelation&>(*relation);

  const auto& expr = highsRelation.getExpression();

  if (expr.isQuadratic()) {
    throw HiGHSError("HiGHS does not support quadratic constraints");
  }

  // HiGHS represents constraints as LHS <= SUM_i (coeff_i * x_i) <= RHS
  // Our expressions are of the form: SUM_i (coeff_i * x_i) + k
  // (Here k is a constant)
  // If constraint sense is '= 0', we set LHS = RHS = -k
  // If constraint sense is '<= 0', we set RHS = -k, LHS = -inf
  // If constraint sense is '>= 0', we set LHS = -k, RHS = inf
  double lhs = 0;
  double rhs = 0;
  switch (highsRelation.getType()) {
    case Relation::EQ_ZERO:
      lhs = -expr.getConstant();
      rhs = -expr.getConstant();
      break;
    case Relation::LE_ZERO:
      lhs = -kHighsInf;
      rhs = -expr.getConstant();
      break;
    case Relation::GE_ZERO:
      lhs = -expr.getConstant();
      rhs = kHighsInf;
      break;
  }

  // Build index and value arrays for the row, consolidating duplicate column
  // indices (which HiGHS rejects). The expression may have the same variable
  // appearing multiple times from repeated additions.
  folly::F14FastMap<HighsInt, double> colToCoeff;
  for (const auto i : folly::irange(expr.size())) {
    const auto colIndex = expr.getVariables().at(i).getColIndex();
    colToCoeff[colIndex] += expr.getCoeffs().at(i);
  }

  std::vector<HighsInt> indices;
  std::vector<double> values;
  indices.reserve(colToCoeff.size());
  values.reserve(colToCoeff.size());
  for (const auto& [col, coeff] : colToCoeff) {
    indices.push_back(col);
    values.push_back(coeff);
  }

  const auto numNz = static_cast<HighsInt>(indices.size());
  if (numNz > 0) {
    REBALANCER_HIGHS_CALL(
        highs_.addRow(lhs, rhs, numNz, indices.data(), values.data()));
  } else {
    REBALANCER_HIGHS_CALL(highs_.addRow(lhs, rhs, 0, nullptr, nullptr));
  }

  auto constraint = std::make_shared<HiGHSConstraint>();
  rows_.push_back({constraint, name});
  return constraint;
}

void HiGHSProblem::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) {
  // Compared by control block, which the weak_ptr keeps alive, so a new
  // constraint allocated at a freed one's address cannot match its row.
  const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const Row& row) {
    return !row.constraint.owner_before(constraint) &&
        !constraint.owner_before(row.constraint);
  });
  if (it == rows_.end()) {
    throw HiGHSError("deleteConstraint: constraint is not in this problem");
  }
  const HighsInt rowIndex = std::distance(rows_.begin(), it);
  REBALANCER_HIGHS_CALL(highs_.deleteRows(1, &rowIndex));
  rows_.erase(it);
}

int HiGHSProblem::getObjectiveSize() const {
  return objectives_.size();
}

std::shared_ptr<const ExpressionImpl> HiGHSProblem::getObjectiveAt(
    int pos) const {
  if (pos >= getObjectiveSize()) {
    throw std::runtime_error(
        fmt::format(
            "There are only {} objectives, but attempt to access objective at pos {}",
            getObjectiveSize(),
            pos));
  }
  return objectives_.at(pos);
}

void HiGHSProblem::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  objectives_.push_back(expression);
}

void HiGHSProblem::clearObjectives() {
  objectives_.clear();
  // also clear all saved results w.r.t. those objectives
  problemResultPerObjective_.clear();
}

void HiGHSProblem::setObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  // Reset all variable objective coefficients to zero
  const auto numCols = highs_.getNumCol();
  for (const auto i : folly::irange(numCols)) {
    REBALANCER_HIGHS_CALL(highs_.changeColCost(i, 0.0));
  }

  // Consolidate duplicate column indices (sum coefficients) before setting
  const auto& expr = dynamic_cast<const HiGHSExpression&>(*expression);
  folly::F14FastMap<HighsInt, double> colToCoeff;
  for (const auto i : folly::irange(expr.size())) {
    colToCoeff[expr.getVariables().at(i).getColIndex()] +=
        expr.getCoeffs().at(i);
  }
  for (const auto& [col, coeff] : colToCoeff) {
    REBALANCER_HIGHS_CALL(highs_.changeColCost(col, coeff));
  }

  if (expr.isQuadratic()) {
    const auto& quadCoeffs = expr.getQuadraticCoeffs();
    const HighsInt dim = numCols;

    HighsHessian hessian;
    hessian.dim_ = dim;
    hessian.format_ = HessianFormat::kSquare;
    hessian.start_.resize(dim + 1, 0);

    std::map<HighsInt, std::vector<std::pair<HighsInt, double>>> colEntries;
    for (const auto& [pair, coeff] : quadCoeffs) {
      const auto [col1, col2] = pair;
      const double hessianValue = (col1 == col2) ? 2.0 * coeff : coeff;
      colEntries[col1].emplace_back(col2, hessianValue);
      if (col1 != col2) {
        colEntries[col2].emplace_back(col1, hessianValue);
      }
    }

    for (const auto col : folly::irange(dim)) {
      hessian.start_[col] = static_cast<HighsInt>(hessian.index_.size());
      if (colEntries.count(col)) {
        for (const auto& [row, val] : colEntries[col]) {
          hessian.index_.push_back(row);
          hessian.value_.push_back(val);
        }
      }
    }
    hessian.start_[dim] = static_cast<HighsInt>(hessian.index_.size());

    REBALANCER_HIGHS_CALL(highs_.passHessian(hessian));
  } else {
    const HighsHessian emptyHessian;
    REBALANCER_HIGHS_CALL(highs_.passHessian(emptyHessian));
  }
}

void HiGHSProblem::setTolerances(const Tolerances& tol) {
  REBALANCER_HIGHS_CALL(
      highs_.setOptionValue("primal_feasibility_tolerance", tol.constraint));
  REBALANCER_HIGHS_CALL(
      highs_.setOptionValue("mip_feasibility_tolerance", tol.integer));
  REBALANCER_HIGHS_CALL(highs_.setOptionValue("mip_abs_gap", tol.absgap));
  REBALANCER_HIGHS_CALL(highs_.setOptionValue("mip_rel_gap", tol.relgap));
  XLOG(WARN) << "tol.relcut unavailable in HiGHS";
  XLOG(WARN) << "tol.abscut unavailable in HiGHS";
}

Tolerances HiGHSProblem::getTolerances() {
  XLOG(WARN) << "tol.relcut unavailable in HiGHS";
  XLOG(WARN) << "tol.abscut unavailable in HiGHS";

  double constraint = 0;
  double integer = 0;
  double absgap = 0;
  double relgap = 0;
  highs_.getOptionValue("primal_feasibility_tolerance", constraint);
  highs_.getOptionValue("mip_feasibility_tolerance", integer);
  highs_.getOptionValue("mip_abs_gap", absgap);
  highs_.getOptionValue("mip_rel_gap", relgap);
  return {
      .constraint = constraint,
      .integer = integer,
      .absgap = absgap,
      .relgap = relgap};
}

void HiGHSProblem::solveForObjectiveAt(
    int pos,
    std::optional<double> timeLimit) {
  // set objective
  auto objective = getObjectiveAt(pos);
  setObjective(objective);

  // set timeLimit if given
  if (timeLimit.has_value()) {
    setTimeout(timeLimit.value());
  }

  // set the parameter for objective at position pos
  setMultiObjectiveParameter(pos);

  // Apply warm start values if any
  applyWarmStart();

  // start the timer as we start solve
  timer_.start();

  // Perform main solve
  REBALANCER_HIGHS_CALL(highs_.run());

  timer_.stop();

  XLOG(DBG1) << "Completed main assignment solve";
}

thrift::ProblemStatus HiGHSProblem::getSolveStatus() {
  const auto status = highs_.getModelStatus();
  switch (status) {
    case HighsModelStatus::kOptimal:
    case HighsModelStatus::kModelEmpty:
    case HighsModelStatus::kObjectiveBound:
    case HighsModelStatus::kObjectiveTarget:
      return thrift::ProblemStatus::OPTIMAL_FOUND;
    case HighsModelStatus::kInfeasible:
    case HighsModelStatus::kUnbounded:
    case HighsModelStatus::kUnboundedOrInfeasible:
      return thrift::ProblemStatus::NO_SOLUTION_EXISTS;
    case HighsModelStatus::kTimeLimit:
    case HighsModelStatus::kIterationLimit:
    case HighsModelStatus::kSolutionLimit:
      return thrift::ProblemStatus::SOLUTION_NOT_FOUND;
    default:
      throw std::runtime_error(
          fmt::format(
              "unknown HiGHS status code {}", static_cast<int>(status)));
  }
}

thrift::ProblemResult HiGHSProblem::getSolveResult() {
  thrift::ProblemResult result;
  result.status() = getSolveStatus();

  double bestBound = 0;
  double bestSolution = 0;
  highs_.getInfoValue("mip_dual_bound", bestBound);
  highs_.getInfoValue("objective_function_value", bestSolution);

  result.bestBound() = bestBound;
  result.bestObjective() = bestSolution;
  result.absoluteGap() = bestSolution - bestBound;
  result.problemAttributes() = getProblemAttributes();

  const double denominator =
      std::max(std::abs(bestBound), std::abs(bestSolution));
  if (denominator == 0) {
    result.relativeGap() = 0;
  } else {
    result.relativeGap() = std::abs(bestSolution - bestBound) / denominator;
  }

  return result;
}

thrift::ProblemAttributes HiGHSProblem::getProblemAttributes() {
  thrift::ProblemAttributes problemAttributes;
  problemAttributes.numVariables() = highs_.getNumCol();
  problemAttributes.numConstraints() = highs_.getNumRow();
  problemAttributes.numOfNonZeros() = highs_.getNumNz();
  return problemAttributes;
}

double HiGHSProblem::getParameter(const std::string& name) {
  double value = 0;
  const auto status = highs_.getOptionValue(name, value);
  if (status != HighsStatus::kOk) {
    throw HiGHSError(fmt::format("Failed to get parameter '{}'", name));
  }
  return value;
}

void HiGHSProblem::setMultiObjectiveParameter(int index) {
  if (multiObjConfig_) {
    for (auto& [key, val] : (multiObjConfig_->paramNamesToValues)) {
      setParameter(
          key, algopt::common::thriftUtils::getObjectiveValue(index, val));
    }
  }
}

void HiGHSProblem::setParameter(const std::string& name, double value) {
  auto status = highs_.setOptionValue(name, value);
  if (status != HighsStatus::kOk) {
    // Some HiGHS options (e.g. "threads") are integer-typed. Retry as int
    // when the double overload fails and the value is integral.
    const auto intValue = static_cast<int>(value);
    if (std::floor(value) == value) {
      status = highs_.setOptionValue(name, intValue);
    }
    if (status != HighsStatus::kOk) {
      throw HiGHSError(
          fmt::format("Failed to set parameter '{}' to {}", name, value));
    }
  }
}

void HiGHSProblem::setLogfile(const std::string& filename) {
  REBALANCER_HIGHS_CALL(highs_.setOptionValue("log_file", filename));
}

void HiGHSProblem::saveToFile(const std::string& filename) {
  constexpr std::array<std::string_view, 2> supportedExtensions = {
      ".lp", ".mps"};

  const auto extension = std::filesystem::path(filename).extension();

  if (std::none_of(
          supportedExtensions.begin(),
          supportedExtensions.end(),
          [&](const auto& ext) { return ext == extension; })) {
    throw std::runtime_error(
        fmt::format(
            "Unsupported file extension {}. Supported extensions are: {}",
            extension.string(),
            folly::join(", ", supportedExtensions)));
  }

  REBALANCER_HIGHS_CALL(highs_.writeModel(filename));
}

void HiGHSProblem::saveToFileWithObjectiveAt(
    int /*pos*/,
    const std::string& /*filename*/) {
  throw std::runtime_error(
      "saveToFileWithObjectiveAt not implemented for HiGHS.");
}

void HiGHSProblem::setDebugPath(const std::string& /*path*/) {
  throw std::runtime_error("setDebugPath not implemented for HiGHS.");
}

void HiGHSProblem::print(
    const std::vector<std::string>& /*substringsToMatch*/) {
  throw std::runtime_error("print is not yet implemented for HiGHS.");
}

void HiGHSProblem::disableLogs() {
  REBALANCER_HIGHS_CALL(highs_.setOptionValue("output_flag", false));
  loggingDisabled_ = true;
}

void HiGHSProblem::setTimeout(double solveTime) {
  REBALANCER_HIGHS_CALL(highs_.setOptionValue("time_limit", solveTime));
}

void HiGHSProblem::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) {
  initialValues_[variable] = value;
}

void HiGHSProblem::applyWarmStart() {
  if (initialValues_.empty()) {
    return;
  }

  const auto numCols = highs_.getNumCol();
  HighsSolution solution;
  solution.col_value.resize(numCols, 0.0);

  for (const auto& [var, value] : initialValues_) {
    const auto& highsVar = dynamic_cast<const HiGHSVariable&>(*var);
    solution.col_value[highsVar.getColIndex()] = value;
  }

  REBALANCER_HIGHS_CALL(highs_.setSolution(solution));
}

void HiGHSProblem::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> /* callback */) {
  // HiGHS does not support solver callbacks yet. Silently ignore so that
  // callers (e.g. FReSH subproblems) can use HiGHS without special-casing.
}

std::optional<IIS> HiGHSProblem::getIIS() {
  // The default strategy only checks for trivially infeasible rows and bounds.
  // FromLp runs the elasticity filter, which keeps integrality, and
  // Irreducible then deletes members until the set is minimal.
  REBALANCER_HIGHS_CALL(highs_.setOptionValue(
      "iis_strategy", kIisStrategyFromLp + kIisStrategyIrreducible));
  HighsIis highsIis;
  // HiGHS validates the IIS as an LP, dropping integrality, so a MIP that is
  // infeasible only because of integrality fails validation with kError. An
  // IIS is diagnostic, so report none rather than failing the caller's solve.
  if (highs_.getIis(highsIis) == HighsStatus::kError) {
    XLOG(WARNING) << "HiGHS could not compute an IIS";
    return std::nullopt;
  }
  // NO_SOLUTION_EXISTS also covers unbounded models, which have no IIS.
  if (highsIis.status_ < kIisModelStatusInfeasible) {
    XLOG(WARNING) << fmt::format(
        "HiGHS found no IIS (IIS status {}, model status {})",
        highsIis.status_,
        highs_.modelStatusToString(highs_.getModelStatus()));
    return std::nullopt;
  }

  IIS iis;
  iis.constraintIds.reserve(highsIis.row_index_.size());
  iis.lowerBoundVars.reserve(highsIis.col_index_.size());
  iis.upperBoundVars.reserve(highsIis.col_index_.size());
  for (const auto row : highsIis.row_index_) {
    iis.constraintIds.push_back(rows_.at(row).name);
  }
  for (const auto i : folly::irange(highsIis.col_index_.size())) {
    const auto& name = colNames_.at(highsIis.col_index_[i]);
    const auto bound = highsIis.col_bound_[i];
    if (bound == kIisBoundStatusLower || bound == kIisBoundStatusBoxed) {
      iis.lowerBoundVars.push_back(name);
    }
    if (bound == kIisBoundStatusUpper || bound == kIisBoundStatusBoxed) {
      iis.upperBoundVars.push_back(name);
    }
  }
  return iis;
}

void HiGHSProblem::replay(const std::string& /* fileName */) const {
  throw std::runtime_error("replay is not yet implemented for HiGHS.");
}

} // namespace facebook::algopt::lp::detail

#endif
