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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/utils/ParallelExecution.h"

#include <fmt/format.h>
#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/FileUtil.h>
#include <folly/init/Init.h>
#include <folly/system/HardwareConcurrency.h>
#include <gflags/gflags.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>

DEFINE_bool(
    matrix,
    false,
    "Run comprehensive matrix benchmark with visualization instead of standard folly benchmarks");
DEFINE_string(csv, "", "Export matrix results to CSV file");

namespace {

// Work complexity scales by multiplier (1x, 4x, 16x, 64x, 256x).
// Baseline = 200 iterations. Only relative scaling is meaningful.
constexpr uint32_t kBaselineIterations = 200;

// The ScaleFactor is used to generate a range of workloads for each
// BENCH_PAIR macro invocation.
template <uint32_t Iterations = 16>
inline int64_t doWork(const int64_t x = 0) noexcept {
  int64_t sum = x;
  folly::makeUnpredictable(sum);
  for (const auto i : folly::irange(Iterations)) {
    sum += x * (i + 1);
    sum ^= (sum >> 3);
  }
  folly::compiler_must_not_elide(sum);
  return sum;
}

// =============================================================================
// SLOW FORWARD-ONLY ITERATOR
// Simulates streaming input with configurable latency per operation.
// This models real-world scenarios of delay while iterating forward only
// iterators.
// =============================================================================
template <typename Iterator, int DelayIterations = 16>
class SlowForwardIterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = typename std::iterator_traits<Iterator>::value_type;
  using difference_type =
      typename std::iterator_traits<Iterator>::difference_type;
  using pointer = typename std::iterator_traits<Iterator>::pointer;
  using reference = typename std::iterator_traits<Iterator>::reference;

  explicit SlowForwardIterator(Iterator it) noexcept : it_(it) {}

  [[nodiscard]] reference operator*() const noexcept {
    return *it_;
  }

  SlowForwardIterator& operator++() noexcept {
    doWork<DelayIterations>(); // burn CPU for iteration latency
    ++it_;
    return *this;
  }

  SlowForwardIterator operator++(int) noexcept {
    const auto tmp = *this;
    doWork<DelayIterations>();
    ++it_;
    return tmp;
  }

  [[nodiscard]] bool operator==(
      const SlowForwardIterator& other) const noexcept {
    return it_ == other.it_;
  }

  [[nodiscard]] bool operator!=(
      const SlowForwardIterator& other) const noexcept {
    return it_ != other.it_;
  }

 private:
  Iterator it_;
};

// =============================================================================
// SLOW FORWARD-ONLY RANGE
// Wraps a container to expose only forward iterators with simulated latency.
// =============================================================================
template <typename Container, int DelayIterations = 16>
class SlowForwardOnlyRange {
 public:
  using value_type = typename Container::value_type;
  using iterator =
      SlowForwardIterator<typename Container::iterator, DelayIterations>;
  using const_iterator =
      SlowForwardIterator<typename Container::const_iterator, DelayIterations>;

  explicit SlowForwardOnlyRange(const Container& container) noexcept
      : container_(container) {}

  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator(container_.begin());
  }

  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator(container_.end());
  }

 private:
  const Container& container_;
};

// =============================================================================
// SHARED BENCHMARK HELPERS
// =============================================================================

[[nodiscard]] inline std::vector<int64_t> makeInput(const size_t n) {
  std::vector<int64_t> v(n);
  std::iota(v.begin(), v.end(), 0);
  return v;
}

[[nodiscard]] inline auto makeInit() noexcept {
  return []() noexcept { return int64_t{0}; };
}

[[nodiscard]] inline auto makeAggregate() noexcept {
  return [](int64_t& acc, const int64_t val) noexcept { acc += val; };
}

// Thread pool (created once, reused across benchmarks)
folly::CPUThreadPoolExecutor& getExecutor() {
  static folly::CPUThreadPoolExecutor executor(folly::available_concurrency());
  return executor;
}

// =============================================================================
// MATRIX MODE CONFIGURATION
// =============================================================================
constexpr int kBenchmarkIterations = 10;
constexpr std::array<int, 5> kDelays = {1, 4, 16, 64, 99};
constexpr std::array<int, 5> kBatchSizes = {8, 16, 32, 64, 128};
constexpr std::array<int, 5> kCollectionSizes = {10, 100, 1000, 10000, 100000};
constexpr std::array<int, 5> kWorkMultipliers = {1, 4, 16, 64, 256};

// =============================================================================
// RESULT STORAGE
// =============================================================================
struct BenchmarkResult {
  double slidingWindowNs;
  double batchingNs;
  double relativePercent; // (SW time / Batch time) * 100
};

// 4D matrix: [delayIdx][batchIdx][collectionIdx][workIdx]
using ResultMatrix =
    std::array<std::array<std::array<std::array<BenchmarkResult, 5>, 5>, 5>, 5>;

using BenchFn = BenchmarkResult (*)(int collectionSize, int batchSize);

inline ResultMatrix& getResults() {
  static ResultMatrix results{};
  return results;
}

// =============================================================================
// TEMPLATED BENCHMARK RUNNER
// =============================================================================
template <int Delay, int WorkMultiplier>
BenchmarkResult runBenchmarkPair(
    const int collectionSize,
    const int batchSize) {
  const auto data = makeInput(collectionSize);

  facebook::algopt::Timer swTimer;
  facebook::algopt::Timer batchTimer;

  for ([[maybe_unused]] const auto iter : folly::irange(kBenchmarkIterations)) {
    // Time SlidingWindow
    {
      const SlowForwardOnlyRange<std::vector<int64_t>, Delay> slowData(data);
      swTimer.start();
      auto result = facebook::rebalancer::executeParallelWindow(
          &getExecutor(),
          slowData,
          doWork<kBaselineIterations*(WorkMultiplier)>,
          makeInit(),
          makeAggregate(),
          static_cast<int>(getExecutor().numThreads()) + 10);
      swTimer.stop();
      folly::doNotOptimizeAway(result);
    }

    // Time Batching
    {
      const SlowForwardOnlyRange<std::vector<int64_t>, Delay> slowData(data);
      batchTimer.start();
      auto result = facebook::rebalancer::executeParallelBatch(
          &getExecutor(),
          slowData,
          doWork<kBaselineIterations*(WorkMultiplier)>,
          makeInit(),
          makeAggregate(),
          facebook::rebalancer::BatchingExecutionOptions{
              .batchSize = static_cast<size_t>(batchSize)});
      batchTimer.stop();
      folly::doNotOptimizeAway(result);
    }
  }

  // Timer accumulates across all iterations
  const double swAvgNs = swTimer.getMilliseconds() * 1e6 / kBenchmarkIterations;
  const double batchAvgNs =
      batchTimer.getMilliseconds() * 1e6 / kBenchmarkIterations;

  // Calculate relative percentage: (SW time / Batch time) * 100
  // If Batching is 2x faster: SW=200ns, Batch=100ns → 200%
  // If SW is 2x faster: SW=100ns, Batch=200ns → 50%
  const double relativePercent =
      (batchAvgNs > 0) ? (swAvgNs / batchAvgNs) * 100.0 : 100.0;

  return {
      .slidingWindowNs = swAvgNs,
      .batchingNs = batchAvgNs,
      .relativePercent = relativePercent};
}

// =============================================================================
// DISPATCH TABLE FOR DELAY × WORK COMBINATIONS (5×5 = 25 function pointers)
// =============================================================================

template <int D, int W>
BenchmarkResult benchWrapper(const int c, const int b) {
  return runBenchmarkPair<D, W>(c, b);
}

// clang-format off
constexpr std::array<std::array<BenchFn, 5>, 5> kBenchFns = {{
    {benchWrapper<1, 1>, benchWrapper<1, 4>, benchWrapper<1, 16>, benchWrapper<1, 64>, benchWrapper<1, 256>},
    {benchWrapper<4, 1>, benchWrapper<4, 4>, benchWrapper<4, 16>, benchWrapper<4, 64>, benchWrapper<4, 256>},
    {benchWrapper<16, 1>, benchWrapper<16, 4>, benchWrapper<16, 16>, benchWrapper<16, 64>, benchWrapper<16, 256>},
    {benchWrapper<64, 1>, benchWrapper<64, 4>, benchWrapper<64, 16>, benchWrapper<64, 64>, benchWrapper<64, 256>},
    {benchWrapper<99, 1>, benchWrapper<99, 4>, benchWrapper<99, 16>, benchWrapper<99, 64>, benchWrapper<99, 256>},
}};
// clang-format on

// =============================================================================
// BENCHMARK RUNNER
// =============================================================================

void runAllBenchmarks() {
  auto& results = getResults();
  constexpr int total = 625;
  int current = 0;

  for (const auto dIdx : folly::irange(5)) {
    for (const auto bIdx : folly::irange(5)) {
      for (const auto cIdx : folly::irange(5)) {
        for (const auto wIdx : folly::irange(5)) {
          results[dIdx][bIdx][cIdx][wIdx] =
              kBenchFns[dIdx][wIdx](kCollectionSizes[cIdx], kBatchSizes[bIdx]);
          ++current;
          if (current % 25 == 0) {
            std::cout << "\rProgress: " << current << "/" << total << " ("
                      << (100 * current / total) << "%)" << std::flush;
          }
        }
      }
    }
  }
  std::cout << "\rProgress: 625/625 (100%) - Complete! \n";
}

// =============================================================================
// VISUALIZATION COMPONENTS
// =============================================================================

// Returns ANSI color escape sequence for the given 256-color code
std::string ansiColor(const int code) {
  return fmt::format("\033[38;5;{}m", code);
}

constexpr std::string_view kResetColor = "\033[0m";
constexpr std::string_view kBlock = "\u2588"; // █ FULL BLOCK

// Gradient bands ordered from highest threshold to lowest.
// Both getGradientColor() and printLegend() are driven by this table.
struct GradientBand {
  double threshold; // minimum relativePercent to match this band
  int colorCode; // ANSI 256-color code
  std::string_view label; // legend description
};

// clang-format off
constexpr std::array<GradientBand, 7> kGradientBands = {{
    {.threshold=400.0, .colorCode=22,  .label="≥400%    Batching dominates (>4x faster)"},
    {.threshold=200.0, .colorCode=34,  .label="200-400% Batching strong win (2-4x faster)"},
    {.threshold=105.0, .colorCode=118, .label="105-200% Batching moderate win (1.05-2x faster)"},
    {.threshold=95.0,  .colorCode=226, .label="95-105%  Near equal (±5%)"},
    {.threshold=50.0,  .colorCode=208, .label="50-95%   SlidingWindow moderate win (up to 2x faster)"},
    {.threshold=25.0,  .colorCode=196, .label="25-50%   SlidingWindow strong win (2-4x faster)"},
    {.threshold=0.0,   .colorCode=88,  .label="<25%     SlidingWindow dominates (>4x faster)"},
}};
// clang-format on

// Returns ANSI colored gradient based on relative percentage
// relativePercent: (SlidingWindow time / Batching time) * 100
//   >100% means Batching is faster
//   <100% means SlidingWindow is faster
std::string getGradientColor(const double relativePercent) {
  for (const auto& band : kGradientBands) {
    if (relativePercent >= band.threshold) {
      return ansiColor(band.colorCode);
    }
  }
  return ansiColor(kGradientBands.back().colorCode);
}

// =============================================================================
// GRID PRINTING
// =============================================================================

void printLegend() {
  std::cout << "\nColor Legend (Batching speed relative to SlidingWindow):\n";
  for (const auto& band : kGradientBands) {
    std::cout << "  " << ansiColor(band.colorCode) << kBlock << kResetColor
              << " " << band.label << "\n";
  }
}

void printComparisonGrid() {
  const auto& results = getResults();

  std::cout << "\n";
  std::cout
      << "╔══════════════════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║    Iterator Delay vs Batch Size Comparison Matrix                        ║\n";
  std::cout
      << "║    Each cell: Rows=Collection(10→100K), Cols=Work(1x→256x)               ║\n";
  std::cout
      << "║    7-color gradient from Deep Red ← → Deep Green                         ║\n";
  std::cout
      << "╠══════════════════════════════════════════════════════════════════════════╣\n";

  // Header row with batch sizes
  std::cout << "          │";
  for (const int b : kBatchSizes) {
    std::cout << " Batch=" << std::setw(3) << b << "  │";
  }
  std::cout << "\n";

  // Separator
  std::cout << "──────────┼";
  for ([[maybe_unused]] const auto i : folly::irange(4)) {
    std::cout << "────────────┼";
  }
  std::cout << "────────────┤\n";

  // For each delay (outer row)
  for (const auto dIdx : folly::irange(5)) {
    // Each delay row has 5 inner rows (collection sizes)
    for (const auto cIdx : folly::irange(5)) {
      // Row label only on middle row (cIdx == 2)
      if (cIdx == 2) {
        std::cout << " Delay=" << std::setw(2) << kDelays[dIdx] << " │";
      } else {
        std::cout << "          │";
      }

      // For each batch size (outer column)
      for (const auto bIdx : folly::irange(5)) {
        std::cout << " ";
        // Print 5 colored blocks for each work multiplier (inner columns)
        for (const auto wIdx : folly::irange(5)) {
          const double pct = results[dIdx][bIdx][cIdx][wIdx].relativePercent;
          std::cout << getGradientColor(pct) << kBlock << kBlock << kResetColor;
        }
        std::cout << " │";
      }
      std::cout << "\n";
    }

    // Separator between delay groups
    if (dIdx < 4) {
      std::cout << "──────────┼";
      for ([[maybe_unused]] const auto i : folly::irange(4)) {
        std::cout << "────────────┼";
      }
      std::cout << "────────────┤\n";
    }
  }

  // Footer
  std::cout
      << "╚══════════════════════════════════════════════════════════════════════════╝\n";
  std::cout << "\nGrid Layout:\n";
  std::cout
      << "  Inner grid rows (top→bottom): N=10, N=100, N=1K, N=10K, N=100K\n";
  std::cout << "  Inner grid cols (left→right): Work1x, Work4x, Work16x, "
               "Work64x, Work256x\n";

  printLegend();
}

void exportToCsv(const std::string& filename) {
  const auto& results = getResults();

  std::string data;
  data.reserve(64 * 1024); // Pre-allocate for ~625 rows

  // Header
  data +=
      "collection_size,work_multiplier,delay,batch_size,"
      "sliding_window_ns,batching_ns,relative_percent\n";

  // Data rows (625 total: 5×5×5×5)
  for (const auto dIdx : folly::irange(5)) {
    for (const auto bIdx : folly::irange(5)) {
      for (const auto cIdx : folly::irange(5)) {
        for (const auto wIdx : folly::irange(5)) {
          const auto& r = results[dIdx][bIdx][cIdx][wIdx];
          data += fmt::format(
              "{},{},{},{},{:.2f},{:.2f},{:.2f}\n",
              kCollectionSizes[cIdx],
              kWorkMultipliers[wIdx],
              kDelays[dIdx],
              kBatchSizes[bIdx],
              r.slidingWindowNs,
              r.batchingNs,
              r.relativePercent);
        }
      }
    }
  }

  if (!folly::writeFile(data, filename.c_str())) {
    throw std::runtime_error(
        fmt::format("Failed to write CSV file: {}", filename));
  }

  std::cout << "Results exported to " << filename << " (" << 625 << " rows)\n";
}

// =============================================================================
// NAME GENERATION MACROS
// Automatically generate benchmark names from (collection_size,
// work_multiplier)
// =============================================================================

// Collection size to suffix mapping (1000 -> 1K, 10000 -> 10K, etc.)
#define N_SUFFIX_10 10
#define N_SUFFIX_100 100
#define N_SUFFIX_1000 1K
#define N_SUFFIX_10000 10K
#define N_SUFFIX_100000 100K

// Token concatenation helpers (multiple levels needed for proper expansion)
#define PASTE2(a, b) a##b
#define PASTE(a, b) PASTE2(a, b)
#define PASTE3(a, b, c) PASTE(PASTE(a, b), c)

// Build names from numeric arguments
#define N_SUFFIX(n) PASTE(N_SUFFIX_, n)
#define N_NAME(n) PASTE(N, N_SUFFIX(n))
#define SLIDING_NAME(n, w) \
  PASTE3(PASTE3(SlidingWindow_, N_NAME(n), _Work), w, x)
#define BATCHING_NAME(n, w) PASTE3(PASTE3(Batching_, N_NAME(n), _Work), w, x)

// =============================================================================
// BENCHMARK MACRO
// Generates both strategies from just (collection_size, work_multiplier).
// Relative % > 100% means Batching wins, < 100% means SlidingWindow wins.
// =============================================================================
#define BENCH_PAIR(n, w)                                             \
  BENCHMARK_MULTI(SLIDING_NAME(n, w)) {                              \
    folly::BenchmarkSuspender suspender;                             \
    const auto data = makeInput(n);                                  \
    const SlowForwardOnlyRange slowData(data);                       \
    suspender.dismiss();                                             \
    const auto result = facebook::rebalancer::executeParallelWindow( \
        &getExecutor(),                                              \
        slowData,                                                    \
        doWork<kBaselineIterations*(w)>,                             \
        makeInit(),                                                  \
        makeAggregate(),                                             \
        static_cast<int>(getExecutor().numThreads()) + 10);          \
    folly::doNotOptimizeAway(result);                                \
    return n;                                                        \
  }                                                                  \
  BENCHMARK_RELATIVE_MULTI(BATCHING_NAME(n, w)) {                    \
    folly::BenchmarkSuspender suspender;                             \
    const auto data = makeInput(n);                                  \
    const SlowForwardOnlyRange slowData(data);                       \
    suspender.dismiss();                                             \
    const auto result = facebook::rebalancer::executeParallelBatch(  \
        &getExecutor(),                                              \
        slowData,                                                    \
        doWork<kBaselineIterations*(w)>,                             \
        makeInit(),                                                  \
        makeAggregate(),                                             \
        facebook::rebalancer::BatchingExecutionOptions{});           \
    folly::doNotOptimizeAway(result);                                \
    return n;                                                        \
  }

} // namespace

// --- 10 Items ---
BENCH_PAIR(10, 1)
BENCH_PAIR(10, 4)
BENCH_PAIR(10, 16)
BENCH_PAIR(10, 64)
BENCH_PAIR(10, 256)

BENCHMARK_DRAW_LINE();

// --- 100 Items ---
BENCH_PAIR(100, 1)
BENCH_PAIR(100, 4)
BENCH_PAIR(100, 16)
BENCH_PAIR(100, 64)
BENCH_PAIR(100, 256)

BENCHMARK_DRAW_LINE();

// --- 1,000 Items ---
BENCH_PAIR(1000, 1)
BENCH_PAIR(1000, 4)
BENCH_PAIR(1000, 16)
BENCH_PAIR(1000, 64)
BENCH_PAIR(1000, 256)

BENCHMARK_DRAW_LINE();

// --- 10,000 Items ---
BENCH_PAIR(10000, 1)
BENCH_PAIR(10000, 4)
BENCH_PAIR(10000, 16)
BENCH_PAIR(10000, 64)
BENCH_PAIR(10000, 256)

BENCHMARK_DRAW_LINE();

// --- 100,000 Items ---
BENCH_PAIR(100000, 1)
BENCH_PAIR(100000, 4)
BENCH_PAIR(100000, 16)
BENCH_PAIR(100000, 64)
BENCH_PAIR(100000, 256)

BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  if (FLAGS_matrix) {
    // Matrix mode: comprehensive benchmark with visualization
    std::cout << "Running 625 benchmark combinations...\n";
    std::cout << "(This may take 10-20 minutes)\n\n";
    runAllBenchmarks();
    printComparisonGrid();
    if (!FLAGS_csv.empty()) {
      exportToCsv(FLAGS_csv);
    }
  } else {
    // Default mode: standard folly benchmarks
    folly::runBenchmarks();
  }

  return 0;
}
