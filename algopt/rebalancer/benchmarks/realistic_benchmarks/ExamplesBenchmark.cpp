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

#include "algopt/rebalancer/benchmarks/utils/BenchmarkReplayer.h"

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>

using namespace facebook::rebalancer::interface::benchmarks;

BENCHMARK(SudokuExample) {
  // Each Sudoku example run takes anywhere between 0.04 to 0.07s depending on
  // MIP solve times. This results in noisy benchmarks as the threshold for
  // regression is about 10% (See TARGETS file). The idea is if we repeat the
  // solve enough number of times, the effect of MIP solve variations would be
  // minimized. Repeating 100 times should still finish each run in about 5s
  constexpr int numReplays = 100;
  for (const auto _ : folly::irange(numReplays)) {
    replay("092ef9f8-8be3-40cd-b6ab-d33f93313a0c");
  }
}

BENCHMARK(ColocateGroupsSpec) {
  replay("1cdfc409-8595-4eca-ae3b-ea3532625fd0-v2");
}

BENCHMARK(ProductionReplay) {
  replay("6bfcd36a-bf2e-4b91-a5a6-d2dc47bf0352-iter0-v3-v2");
}

BENCHMARK(GSPEvalApplyNumericalIssue1) {
  replay("GSP_eval_apply_numerical_issue_1");
}

BENCHMARK(GSPEvalApplyNumericalIssue2) {
  replay("GSP_eval_apply_numerical_issue_2");
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
