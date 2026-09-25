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
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <folly/container/F14Map.h>

#include <optional>

namespace facebook::algopt::lp::detail {

class HiGHSProblem : public ProblemImpl {
 public:
  HiGHSProblem();
  ~HiGHSProblem();
  // Prevent copies and moves — Highs is neither copyable nor movable
  HiGHSProblem(const HiGHSProblem&) = delete;
  HiGHSProblem& operator=(const HiGHSProblem&) = delete;
  HiGHSProblem(HiGHSProblem&&) = delete;
  HiGHSProblem& operator=(HiGHSProblem&&) = delete;

  std::string_view backendName() const override {
    return "HIGHS";
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

  double getParameter(const std::string& name) override;
  void setParameter(const std::string& name, double value) override;
  void setTolerances(const Tolerances& tol) override;
  Tolerances getTolerances() override;

  void setLogfile(const std::string& filename) override;
  void saveToFile(const std::string& filename) override;
  void saveToFileWithObjectiveAt(int pos, const std::string& filename) override;
  void setDebugPath(const std::string& path) override;
  void print(const std::vector<std::string>& substringsToMatch) override;
  void disableLogs() override;

  int getObjectiveSize() const override;
  std::shared_ptr<const ExpressionImpl> getObjectiveAt(int pos) const override;
  void clearObjectives() override;

  void addStartValue(std::shared_ptr<const VariableImpl> variable, double value)
      override;

  void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback)
      override;
  std::optional<IIS> getIIS() override;
  void replay(const std::string& fileName) const override;

 protected:
  void solveForObjectiveAt(int pos, std::optional<double> timeLimit) override;
  thrift::ProblemStatus getSolveStatus() override;
  thrift::ProblemResult getSolveResult() override;
  thrift::ProblemAttributes getProblemAttributes() override;

 private:
  std::shared_ptr<VariableImpl> makeVar(
      const std::string& name,
      VariableImpl::Type type,
      std::optional<double> lb = std::nullopt,
      std::optional<double> ub = std::nullopt);

  void setObjective(std::shared_ptr<const ExpressionImpl> expression);
  void setTimeout(double solveTime);
  void setMultiObjectiveParameter(int pos);
  void applyWarmStart();

  // The order in objectives_ denotes priority and so it is important.
  std::vector<std::shared_ptr<const ExpressionImpl>> objectives_;

  Highs highs_;
  folly::F14FastMap<std::shared_ptr<const VariableImpl>, double> initialValues_;
  Timer timer_;
  bool loggingDisabled_ = false;
};

} // namespace facebook::algopt::lp::detail

#endif
