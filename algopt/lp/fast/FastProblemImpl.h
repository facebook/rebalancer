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

#include "algopt/lp/fast/FastExpressionImpl.h"
#include "algopt/lp/fast/FastTypes.h"
#include "algopt/lp/generic/Problem.h"

#include <folly/container/F14Map.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace facebook::algopt::lp {

class FastProblemImpl : public ProblemImpl {
 public:
  FastProblemImpl() = default;

  std::string_view backendName() const override {
    return "FAST";
  }

  std::shared_ptr<VariableImpl> makeVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeIntVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeSemiContVar(
      const std::string& name,
      double threshold) override;
  std::shared_ptr<VariableImpl> makeSemiIntVar(
      const std::string& name,
      double threshold) override;
  std::shared_ptr<VariableImpl> makeBoolVar(const std::string& name) override;

  std::shared_ptr<ExpressionImpl> makeExpression(
      double constant) const override;

  std::shared_ptr<ConstraintImpl> newConstraint(
      std::shared_ptr<const RelationImpl> relation,
      const std::string& name) override;
  void deleteConstraint(std::shared_ptr<ConstraintImpl> constraint) override;

  void addObjective(std::shared_ptr<const ExpressionImpl> expression) override;

  int getObjectiveSize() const override;
  std::shared_ptr<const ExpressionImpl> getObjectiveAt(int pos) const override;
  void clearObjectives() override;

  double getParameter(const std::string& name) override;
  void setParameter(const std::string& name, double value) override;

  void setLogfile(const std::string& filename) override;
  void saveToFile(const std::string& filename) override;
  void saveToFileWithObjectiveAt(int pos, const std::string& filename) override;
  void setDebugPath(const std::string& path) override;
  void print(const std::vector<std::string>& substringsToMatch) override;
  void disableLogs() override;

  void setTolerances(const Tolerances& tol) override;
  Tolerances getTolerances() override;

  void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback)
      override;
  void addStartValue(std::shared_ptr<const VariableImpl> variable, double value)
      override;

  std::optional<IIS> getIIS() override;

  thrift::ProblemAttributes getProblemAttributes() override;

  // Accessors and post-build mutators below are intentionally
  // unsynchronized. They must only be called after all concurrent
  // makeVar/newConstraint calls have completed (join on all async
  // futures). The mutexes protect concurrent writes during build; these
  // methods run in a separate phase after the build futures are joined.
  uint64_t getVariableCount() const;
  const InnerVariable& getVariable(uint64_t variableId) const;
  uint64_t getConstraintCount() const;
  const InnerConstraint& getConstraint(uint64_t constraintId) const;

  // Deterministic variable ordering by name. Returns a vector of variable
  // indices for more efficient access.
  std::vector<uint64_t> buildSortedVarIds() const;

  // Deterministic constraint ordering by name. Returns a vector of constraint
  // indices for more efficient access.
  std::vector<uint64_t> buildSortedConstrIds() const;

 protected:
  void solveForObjectiveAt(int pos, std::optional<double> timeLimit) override;
  thrift::ProblemStatus getSolveStatus() override;
  thrift::ProblemResult getSolveResult() override;

 private:
  std::shared_ptr<VariableImpl> makeVarInternal(
      const std::string& name,
      VariableImpl::Type type,
      std::optional<double> threshold);

  std::mutex varMutex_;
  std::mutex constraintMutex_;
  std::vector<std::unique_ptr<InnerVariable>> variables_;
  std::vector<InnerConstraint> constraints_;
  std::vector<std::shared_ptr<FastExpressionImpl>> objectives_;
  folly::F14FastMap<std::string, double> params_;
  std::optional<Tolerances> tolerances_;
};

} // namespace facebook::algopt::lp
