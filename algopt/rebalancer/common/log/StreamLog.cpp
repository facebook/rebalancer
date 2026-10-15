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

#include "algopt/rebalancer/common/log/StreamLog.h"

#include "algopt/rebalancer/common/Tabulator.h"

#include <folly/container/irange.h>
#include <folly/gen/Base.h>
#include <folly/gen/String.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>

#include <iterator>
#include <sstream>

using folly::dynamic;
using namespace folly::gen;
using std::string;
using std::vector;

namespace facebook {
namespace rebalancer {

void StreamLog::log(const ProblemDefinition& info) {
  fmt::memory_buffer buf;
  fmt::format_to(std::back_inserter(buf), "\n== Problem Definition ==\n");
  Tabulator{{.rowSeparator = '-', .colSeparator = '|'}}
      .setHeader({"", "count"})
      .addRow(
          dynamic::array(fmt::format("{}s", info.objectName), info.objectCount))
      .addRow(
          dynamic::array(
              fmt::format("{}s", info.containerName), info.containerCount))
      .tabulate(buf);
  XLOG(DBG1) << fmt::to_string(buf);
}

void StreamLog::log(const interface::GlobalObjectiveSummary& info) {
  std::vector<double> values;
  fmt::memory_buffer buf;
  fmt::format_to(std::back_inserter(buf), "\n== Objective Summary ==\n");
  auto tb = Tabulator{{}}.setHeader(
      {"[#]", "tuple", "weight", "value", "description"});
  int pos = 0;
  int count = 1;
  for (auto& goal : *info.goals()) {
    for (const auto& it : *goal.objs()) {
      tb.addRow(
          dynamic::array(
              fmt::format("[{}]", count),
              pos,
              *it.weight(),
              *it.value(),
              *it.desc()));
      ++count;
    }
    values.push_back(*goal.value());
    ++pos;
  }

  fmt::format_to(
      std::back_inserter(buf),
      "Objective:\t({})\n",
      folly::join(", ", from(values) | eachTo<string>() | as<vector>()));
  tb.tabulate(buf);
  XLOG(DBG1) << fmt::to_string(buf);
}

void StreamLog::log(const interface::ConstraintSummary& info) {
  fmt::memory_buffer buf;
  fmt::format_to(std::back_inserter(buf), "\n== Constraint Summary ==\n");
  fmt::format_to(
      std::back_inserter(buf),
      "Broken count:{:>4}; Broken value:{:>4}\n",
      *info.brokenCount(),
      *info.brokenVal());
  auto tb =
      Tabulator{{}}.setHeader({"[#]", "broken? ", "value", "description"});
  int count = 1;
  for (const auto& it : *info.constraints()) {
    tb.addRow(
        dynamic::array(
            fmt::format("[{}]", count),
            (*it.value() > 0 ? "Y " : "N "),
            *it.value(),
            *it.desc()));
    ++count;
  }
  tb.tabulate(buf);
  XLOG(DBG1) << fmt::to_string(buf);
}

void StreamLog::log(const interface::MovesSummary& info) {
  if (!XLOG_IS_ON(DBG2)) {
    return;
  }

  {
    // Print moves applied.
    constexpr int maxMoves = 10;
    std::stringstream ss;
    for (size_t i = 0; i < info.moves()->size() && i < maxMoves; i++) {
      const auto& move = info.moves()->at(i);
      if (i > 0) {
        ss << ", ";
      }
      ss << *move.object() << " from " << *move.srcContainer() << " to "
         << *move.dstContainer();
    }
    if (info.moves()->size() > maxMoves) {
      ss << ", ...";
    }
    XLOG(DBG2) << "Applied " << info.moves()->size() << " move"
               << (info.moves()->size() == 1 ? "" : "s") << ": " << ss.str();
  }

  if (!info.objectives()->empty() || !info.constraintInvldCnt()->empty()) {
    // Print goal/constraint metrics if available.
    fmt::memory_buffer buf;
    if (info.objectives()->size() > 0) {
      fmt::format_to(std::back_inserter(buf), "\nChanged Objectives:");

      auto objectivesTable = Tabulator{{}}.setHeader(
          {"current value", "decreased by", "description"});
      for (const auto& [objective, delta] : *info.objectives()) {
        // no need to print unchanged objectives
        if (*delta.change() == 0) {
          continue;
        }
        objectivesTable.addRow(
            dynamic::array(*delta.newValue(), *delta.change(), objective));
      }
      objectivesTable.tabulate(buf);
    }
    for (const auto& [constraint, invalidCount] : *info.constraintInvldCnt()) {
      fmt::format_to(
          std::back_inserter(buf),
          "{} is violated {} times {}%\n",
          constraint,
          invalidCount,
          (double)invalidCount / (double)*info.evalsCount() * 100);
    }
    XLOG(DBG2) << fmt::to_string(buf);
  }
}

void StreamLog::log(const SolverSummary& info) {
  fmt::memory_buffer buf;
  const auto bufInserter = std::back_inserter(buf);
  fmt::format_to(bufInserter, "== Solver Summary ==\n");
  switch (info.solverType) {
    case SolverType::LOCAL_SEARCH:
      fmt::format_to(bufInserter, "LOCAL_SEARCH");
      break;
    case SolverType::LOCAL_SEARCH_STAGES:
      fmt::format_to(bufInserter, "LOCAL_SEARCH_STAGES");
      break;
    case SolverType::OPTIMAL:
      fmt::format_to(bufInserter, "OPTIMAL");
      break;
    case SolverType::OPTIMAL_SUBSET:
      fmt::format_to(bufInserter, "OPTIMAL_SUBSET");
      break;
    case SolverType::SIMULATED_ANNEALING:
      fmt::format_to(bufInserter, "SIMULATED_ANNEALING");
      break;
    default:
      fmt::format_to(bufInserter, "UNKNOWN");
      break;
  }
  fmt::format_to(bufInserter, " ");
  switch (info.endReason) {
    case interface::EndReason::OPTIMAL:
      fmt::format_to(bufInserter, "reach optimal");
      break;
    case interface::EndReason::HIT_TIME_LIMIT:
      fmt::format_to(bufInserter, "hit time threshold {}", *(info.auxInfo));
      break;
    case interface::EndReason::HIT_MOVE_LIMIT:
      fmt::format_to(bufInserter, "hit move threshold {}", *(info.auxInfo));
      break;
    case interface::EndReason::HIT_STOP_CONSTRAINT:
      fmt::format_to(bufInserter, "stopping constraint met");
      break;
    case interface::EndReason::HIT_PLATEAU_TIME:
      fmt::format_to(bufInserter, "hit plateau time {}", *(info.auxInfo));
      break;
    default:
      break;
  }
  XLOG(DBG1) << fmt::to_string(buf) << "\n";
}

void StreamLog::log(const BenchmarkInfo& info) {
  XLOG(DBG1) << fmt::format(
      "\n== BenchMark of {} ==\nused {}s", info.key, info.totalTime);
}
} // namespace rebalancer
} // namespace facebook
