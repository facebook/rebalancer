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

#include <folly/FileUtil.h>
#include <folly/hash/Hash.h>
#include <folly/logging/xlog.h>

#include <ostream>

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/shared/MFHash.h"
#else
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#endif

namespace facebook::rebalancer {

struct Bounds {
  double lower_bound;
  double upper_bound;
};

#ifndef REBALANCER_OSS_BUILD
template <class Key, class Value>
using PackerMap = multifeed::MFHashMap<Key, Value>;

template <class Value>
using PackerSet = multifeed::MFHashSet<Value>;
#else
template <class Key, class Value>
using PackerMap = folly::F14FastMap<Key, Value>;

template <class Value>
using PackerSet = folly::F14FastSet<Value>;
#endif

// Order-independent hash for an unordered set, matching its order-independent
// operator==. Unordered sets have no default hash, so use this as an explicit
// hasher for maps or composite keys.
struct UnorderedSetHash {
  template <class Set>
  std::size_t operator()(const Set& set) const {
    return folly::hash::commutative_hash_combine_range(set.begin(), set.end());
  }
};

template <class K, class V>
std::ostream& operator<<(std::ostream& os, const PackerMap<K, V>& m) {
  os << "{";
  bool first = true;
  for (auto& p : m) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << p.first << ": " << p.second;
  }
  os << "}";
  return os;
}

template <class T>
std::ostream& operator<<(std::ostream& os, const PackerSet<T>& m) {
  os << "[";
  bool first = true;
  for (auto& p : m) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << p;
  }
  os << "]";
  return os;
}

template <class D, class R, class T>
R summation(const D& d, std::function<R(T&)> func) {
  R result = 0;
  for (const auto& it : d) {
    result += func(it);
  }
  return result;
}

template <class D, class R, class T>
std::vector<R> list(const D& d, std::function<R(T&)> func) {
  std::vector<R> result;
  for (const auto& it : d) {
    result.push_back(func(it));
  }
  return result;
}

inline void writeToFile(
    const std::string& filename,
    const std::string& content) {
  XLOG(INFO) << "writing to " << filename;
#ifdef _WIN32
  folly::writeFile(content, filename.c_str());
#else
  folly::writeFileAtomic(filename, content, 0644, folly::SyncType::WITH_SYNC);
#endif
  XLOG(INFO) << "finished writing to " << filename;
}

// Copies logged output to stdout as well as provided logFile
class SolverOutputLogger {
 public:
  explicit SolverOutputLogger(std::string fileName = "")
      : logFileName_(fileName) {}
  ~SolverOutputLogger() {
    if (!logFileName_.empty()) {
      writeToFile(logFileName_, logFileContents_);
    }
  }

  SolverOutputLogger(const SolverOutputLogger&) = delete;
  SolverOutputLogger& operator=(const SolverOutputLogger&) = delete;
  SolverOutputLogger(SolverOutputLogger&&) = delete;
  SolverOutputLogger& operator=(SolverOutputLogger&&) = delete;

  // The functions log() and logError() also overwrite the FILE and LINE from
  // which the message is called. So in some situations, it maybe just useful to
  // add the contents and let the caller handle output on stdout
  void appendToLog(const std::string& line) {
    if (!logFileName_.empty()) {
      // only append to logfile if needed
      logFileContents_ += line;
    }
  }

  void log(const std::string& line) {
    XLOG(INFO) << line;
    appendToLog(fmt::format("{}\n", line));
  }

  void logError(const std::string& line) {
    std::string errorMsg = fmt::format("\n{0:*^100}\n", " ERROR ");
    errorMsg += fmt::format("{0:*^100}\n", line);
    errorMsg += fmt::format("{0:*^100}\n", "");
    XLOG(ERR) << errorMsg;
    appendToLog(errorMsg);
  }

 private:
  std::string logFileContents_;
  std::string logFileName_;
};

} // namespace facebook::rebalancer
