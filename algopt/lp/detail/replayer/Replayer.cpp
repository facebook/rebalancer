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

#include "algopt/lp/detail/generic/impl/GenericProblemImpl.h"
#include <algopt/lp/factory/ProblemFactory.h>

#include <fmt/format.h>
#include <folly/init/Init.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include <stdexcept>

using namespace facebook::algopt::lp;
using namespace facebook::algopt::lp::detail;

constexpr std::string_view kXpressSolver = "Xpress";
constexpr std::string_view kGurobiSolver = "Gurobi";
DEFINE_string(input_file, "", "model saved as mps file");
DEFINE_double(time_limit, 600, "timeout seconds");
DEFINE_int32(num_threads, 0, "number of threads used by solver");
DEFINE_string(solver_type, std::string(kGurobiSolver), "xpress or gurobi");
DEFINE_bool(use_generic, false, "whether to use generic builder");

std::string_view getSolverName() {
  if (FLAGS_solver_type.starts_with("Xpress") ||
      FLAGS_solver_type.starts_with("xpress")) {
    return kXpressSolver;
  }
  if (FLAGS_solver_type.starts_with("Gurobi") ||
      FLAGS_solver_type.starts_with("gurobi")) {
    return kGurobiSolver;
  }
  throw std::runtime_error(fmt::format(
      "Invalid --solver_type '{}'. Valid options: xpress, gurobi",
      FLAGS_solver_type));
}

int main(int argc, char* argv[]) {
  const folly::Init init(&argc, &argv);
  if (FLAGS_input_file.empty()) {
    throw std::runtime_error(
        "Specify model with commandline flag --input_file=FILENAME");
  }
  const auto solverName = getSolverName();
  const bool useXpress = solverName == kXpressSolver;
  XLOG(INFO) << fmt::format(
      "Replaying saved model {} using solver {} with timeout {} seconds and {} threads",
      FLAGS_input_file,
      solverName,
      FLAGS_time_limit,
      FLAGS_num_threads);

  auto factory = useXpress ? ProblemFactory::makeXpressProblem
                           : ProblemFactory::makeGurobiProblem;

  auto problem = FLAGS_use_generic
      ? Problem(std::make_unique<GenericProblemImpl>(factory))
      : factory();

  if (FLAGS_num_threads > 0) {
    auto paramName = useXpress ? "XPRS_THREADS" : "GRB_Threads";
    problem.setParameter(paramName, FLAGS_num_threads);
  }
  auto timeLimitParam = useXpress ? "XPRS_TIMELIMIT" : "GRB_TimeLimit";
  problem.setMaxSolveTime(FLAGS_time_limit);
  problem.setParameter(timeLimitParam, FLAGS_time_limit);
  problem.replay(FLAGS_input_file);
}
