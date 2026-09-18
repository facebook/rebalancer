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

#include "algopt/lp/fast/FastProblemImpl.h"

#include "algopt/lp/fast/FastConstraintImpl.h"
#include "algopt/lp/fast/FastRelationImpl.h"
#include "algopt/lp/fast/FastVariableImpl.h"

#include <fmt/core.h>
#include <folly/container/F14Map.h>
#include <folly/container/irange.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace facebook::algopt::lp {

std::shared_ptr<VariableImpl> FastProblemImpl::makeVarInternal(
    const std::string& name,
    VariableImpl::Type type,
    std::optional<double> threshold) {
  double lb = 0;
  double ub = std::numeric_limits<double>::infinity();
  if (type == VariableImpl::Type::BINARY) {
    lb = 0;
    ub = 1;
  }
  auto inner = std::make_unique<InnerVariable>(InnerVariable{
      .name = name,
      .type = type,
      .lb = lb,
      .ub = ub,
      .threshold = threshold,
      .initialValue = std::nullopt,
      .value = std::nullopt,
  });
  auto* ptr = inner.get();
  uint64_t variableId;
  {
    const std::lock_guard<std::mutex> lock(varMutex_);
    variableId = static_cast<uint64_t>(variables_.size());
    variables_.push_back(std::move(inner));
  }
  return std::make_shared<FastVariableImpl>(variableId, ptr);
}

std::shared_ptr<VariableImpl> FastProblemImpl::makeVar(
    const std::string& name) {
  return makeVarInternal(name, VariableImpl::Type::CONTINUOUS, std::nullopt);
}

std::shared_ptr<VariableImpl> FastProblemImpl::makeIntVar(
    const std::string& name) {
  return makeVarInternal(name, VariableImpl::Type::INTEGER, std::nullopt);
}

std::shared_ptr<VariableImpl> FastProblemImpl::makeBoolVar(
    const std::string& name) {
  return makeVarInternal(name, VariableImpl::Type::BINARY, std::nullopt);
}

std::shared_ptr<VariableImpl> FastProblemImpl::makeSemiContVar(
    const std::string& name,
    double threshold) {
  return makeVarInternal(name, VariableImpl::Type::SEMI_CONTINUOUS, threshold);
}

std::shared_ptr<VariableImpl> FastProblemImpl::makeSemiIntVar(
    const std::string& name,
    double threshold) {
  return makeVarInternal(name, VariableImpl::Type::SEMI_INTEGER, threshold);
}

std::shared_ptr<ExpressionImpl> FastProblemImpl::makeExpression(
    double constant) const {
  return std::make_shared<FastExpressionImpl>(constant);
}

std::shared_ptr<ConstraintImpl> FastProblemImpl::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) {
  const auto& fastRelation = static_cast<const FastRelationImpl&>(*relation);
  auto innerConstraint = InnerConstraint{
      .terms = fastRelation.getTerms(),
      .rhs = -fastRelation.getConstant(),
      .sense = fastRelation.getSense(),
      .name = name,
      .deleted = false,
  };
  uint64_t constraintId;
  {
    const std::lock_guard<std::mutex> lock(constraintMutex_);
    constraintId = static_cast<uint64_t>(constraints_.size());
    constraints_.push_back(std::move(innerConstraint));
  }
  return std::make_shared<FastConstraintImpl>(constraintId);
}

// Safe to call concurrently with newConstraint during the parallel build:
// both serialize on constraintMutex_. Only marks the constraint deleted (no
// vector resize), so it never invalidates a concurrent push_back.
void FastProblemImpl::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) {
  const auto& fastConstraint =
      static_cast<const FastConstraintImpl&>(*constraint);
  const std::lock_guard<std::mutex> lock(constraintMutex_);
  constraints_[fastConstraint.getConstraintId()].deleted = true;
}

// Not mutex-protected: addObjective is called from Problem::setObjective
// on the caller thread after all parallel build futures have been joined.
void FastProblemImpl::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  auto cloned = expression->clone();
  auto fastExpr = std::dynamic_pointer_cast<FastExpressionImpl>(cloned);
  if (!fastExpr) {
    throw std::runtime_error(
        "FastProblemImpl::addObjective: expression is not a FastExpressionImpl");
  }
  objectives_.push_back(std::move(fastExpr));
}

int FastProblemImpl::getObjectiveSize() const {
  return static_cast<int>(objectives_.size());
}

std::shared_ptr<const ExpressionImpl> FastProblemImpl::getObjectiveAt(
    int pos) const {
  return objectives_.at(pos);
}

void FastProblemImpl::clearObjectives() {
  objectives_.clear();
}

double FastProblemImpl::getParameter(const std::string& name) {
  auto it = params_.find(name);
  if (it == params_.end()) {
    throw std::runtime_error(fmt::format("Parameter '{}' not found", name));
  }
  return it->second;
}

void FastProblemImpl::setParameter(const std::string& name, double value) {
  params_[name] = value;
}

void FastProblemImpl::setLogfile(const std::string& /*filename*/) {}

void FastProblemImpl::saveToFile(const std::string& /*filename*/) {
  throw std::runtime_error("FastProblemImpl::saveToFile is not supported");
}

void FastProblemImpl::saveToFileWithObjectiveAt(
    int /*pos*/,
    const std::string& /*filename*/) {
  throw std::runtime_error(
      "FastProblemImpl::saveToFileWithObjectiveAt is not supported");
}

void FastProblemImpl::setDebugPath(const std::string& /*path*/) {}

void FastProblemImpl::print(
    const std::vector<std::string>& /*substringsToMatch*/) {}

void FastProblemImpl::disableLogs() {}

void FastProblemImpl::setTolerances(const Tolerances& tol) {
  tolerances_ = tol;
}

Tolerances FastProblemImpl::getTolerances() {
  return tolerances_.value_or(Tolerances{});
}

void FastProblemImpl::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> /*callback*/) {}

void FastProblemImpl::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) {
  const auto* fastVariable =
      dynamic_cast<const FastVariableImpl*>(variable.get());
  if (fastVariable == nullptr) {
    throw std::invalid_argument(
        "FastProblemImpl::addStartValue requires a FastVariableImpl");
  }
  const std::lock_guard<std::mutex> lock(varMutex_);
  variables_.at(fastVariable->getVariableId())->initialValue = value;
}

std::optional<IIS> FastProblemImpl::getIIS() {
  return std::nullopt;
}

void FastProblemImpl::solveForObjectiveAt(
    int /*pos*/,
    std::optional<double> /*timeLimit*/) {
  throw std::runtime_error(
      "FastProblemImpl does not support solving. "
      "Use a solver backend (Gurobi, Xpress, HiGHS) instead.");
}

thrift::ProblemStatus FastProblemImpl::getSolveStatus() {
  throw std::runtime_error(
      "FastProblemImpl does not support solving. "
      "Use a solver backend (Gurobi, Xpress, HiGHS) instead.");
}

thrift::ProblemResult FastProblemImpl::getSolveResult() {
  throw std::runtime_error(
      "FastProblemImpl does not support solving. "
      "Use a solver backend (Gurobi, Xpress, HiGHS) instead.");
}

thrift::ProblemAttributes FastProblemImpl::getProblemAttributes() {
  thrift::ProblemAttributes attrs;
  attrs.numVariables() = static_cast<int>(variables_.size());
  int numConstraints = 0;
  int64_t numNonZeros = 0;
  for (const auto& constraint : constraints_) {
    if (!constraint.deleted) {
      ++numConstraints;
      numNonZeros += static_cast<int64_t>(constraint.terms.size());
    }
  }
  attrs.numConstraints() = numConstraints;
  attrs.numOfNonZeros() = numNonZeros;
  return attrs;
}

uint64_t FastProblemImpl::getVariableCount() const {
  return static_cast<uint64_t>(variables_.size());
}

const InnerVariable& FastProblemImpl::getVariable(uint64_t variableId) const {
  return *variables_.at(variableId);
}

uint64_t FastProblemImpl::getConstraintCount() const {
  return static_cast<uint64_t>(constraints_.size());
}

const InnerConstraint& FastProblemImpl::getConstraint(
    uint64_t constraintId) const {
  return constraints_.at(constraintId);
}

std::vector<uint64_t> FastProblemImpl::buildSortedVarIds() const {
  const auto numVars = getVariableCount();
  std::vector<std::pair<std::string_view, uint64_t>> sortKeys;
  sortKeys.reserve(numVars);
  for (const auto i : folly::irange(numVars)) {
    sortKeys.emplace_back(getVariable(i).name, i);
  }
  std::sort(sortKeys.begin(), sortKeys.end());
  std::vector<uint64_t> sorted;
  sorted.reserve(numVars);
  for (const auto& [_, id] : sortKeys) {
    sorted.push_back(id);
  }
  return sorted;
}

std::vector<uint64_t> FastProblemImpl::buildSortedConstrIds() const {
  std::vector<std::pair<std::string_view, uint64_t>> sortKeys;
  sortKeys.reserve(getConstraintCount());
  for (const auto i : folly::irange(getConstraintCount())) {
    const auto& constr = getConstraint(i);
    if (!constr.deleted) {
      sortKeys.emplace_back(constr.name, i);
    }
  }
  std::sort(sortKeys.begin(), sortKeys.end());
  std::vector<uint64_t> sorted;
  sorted.reserve(sortKeys.size());
  for (const auto& [_, id] : sortKeys) {
    sorted.push_back(id);
  }
  return sorted;
}

} // namespace facebook::algopt::lp
