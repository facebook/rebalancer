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

#include "algopt/rebalancer/algopt_common/Concepts.h"
#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"
#include "algopt/rebalancer/entities/Map.h"

#include <folly/container/MapUtil.h>
#include <folly/Conv.h>
#include <folly/lang/SafeAssert.h>

#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

namespace facebook::algopt {

namespace entities = ::facebook::rebalancer::entities;

template <typename T>
concept SizeTConvertible = std::constructible_from<T, std::size_t> &&
    std::constructible_from<std::size_t, T>;

/**
`CompressedIdMap` is an unordered map from integer-id keys (any type satisfying
the `SizeTConvertible` concept) to values, with a caller-supplied default. Unset
keys read the default value and it uses a dense or sparse layout depending on
the total size and the expected number of non default values.

The layout is chosen once at construction and fixed for the lifetime of the
map. If expectedNonDefaultSize / totalSize meets DenseThreshold (default 0.2),
the map uses a dense vector<ValueT>(totalSize); otherwise it uses a
Map<KeyT, ValueT> hashmap. Dense is always faster on lookup and saves memory
at high density; sparse trades lookup speed for lower memory at low density.

Complexity, with N = totalSize and K = the number of non-default entries:
- emplace(key, value):
  - dense: O(1).
  - sparse: O(1) on average.
- getValue(key):
  - dense: O(1), a single array lookup.
  - sparse: O(1) on average.
- forEachNonDefault(fn):
  - dense: O(N/64 + K).
  - sparse: O(K).
*/
template <
    SizeTConvertible KeyT,
    std::default_initializable ValueT,
    double DenseThreshold = 0.2>
class CompressedIdMap {
 public:
  using key_type = KeyT;
  using mapped_type = ValueT;

 private:
  // only one of SparseStorage or DenseStorage is used
  using SparseStorage = entities::Map<KeyT, ValueT>;
  struct DenseStorage {
    std::vector<ValueT> values;
    DynamicBitSet nonDefaultBits;

    DenseStorage(std::size_t totalSize, const ValueT& defaultValue)
        : values(totalSize, defaultValue), nonDefaultBits(totalSize) {}

    bool operator==(const DenseStorage&) const = default;
  };

  static bool shouldPickDense(
      std::size_t totalSize,
      std::size_t expectedNonDefaultSize) {
    return totalSize > 0 && expectedNonDefaultSize > 0 &&
        static_cast<double>(expectedNonDefaultSize) >=
        DenseThreshold * static_cast<double>(totalSize);
  }

 public:
  CompressedIdMap(
      std::size_t totalSize,
      ValueT defaultValue,
      std::size_t expectedNonDefaultSize)
      : isDense_(shouldPickDense(totalSize, expectedNonDefaultSize)),
        totalSize_(totalSize),
        defaultValue_(defaultValue),
        dense_(isDense_ ? totalSize : 0, defaultValue) {
    if (!isDense_) {
      sparse_.reserve(
          folly::to<typename SparseStorage::size_type>(expectedNonDefaultSize));
    }
  }

  template <IsIterableOverPairs<KeyT, ValueT> Range>
  CompressedIdMap(
      const Range& keyToValue,
      ValueT defaultValue,
      std::size_t totalSize)
      : CompressedIdMap(totalSize, defaultValue, keyToValue.size()) {
    for (const auto& [k, v] : keyToValue) {
      emplace(k, v);
    }
  }

  // Consumes existing sparse storage, retaining it unless the resulting
  // density favors dense storage.
  CompressedIdMap(
      SparseStorage&& keyToValue,
      ValueT defaultValue,
      std::size_t totalSize)
      : totalSize_(totalSize),
        defaultValue_(std::move(defaultValue)),
        dense_(/*totalSize=*/0, defaultValue_) {
    for (auto it = keyToValue.begin(); it != keyToValue.end();) {
      const auto& [key, value] = *it;
      const auto index = static_cast<std::size_t>(key);
      FOLLY_SAFE_CHECK(
          index < totalSize_,
          "CompressedIdMap: key index ",
          index,
          " exceeds totalSize ",
          totalSize_);
      if (value == defaultValue_) {
        it = keyToValue.erase(it);
      } else {
        ++it;
      }
    }

    isDense_ = shouldPickDense(totalSize_, keyToValue.size());
    if (isDense_) {
      dense_ = DenseStorage(totalSize_, defaultValue_);
      for (auto& [key, value] : keyToValue) {
        emplace(key, std::move(value));
      }
    } else {
      sparse_ = std::move(keyToValue);
    }
  }

  bool operator==(const CompressedIdMap&) const = default;

  template <typename... Args>
  FOLLY_ALWAYS_INLINE void emplace(KeyT key, Args&&... args) noexcept {
    const auto index = static_cast<std::size_t>(key);
    FOLLY_SAFE_CHECK(
        index < totalSize_,
        "CompressedIdMap::emplace: key index ",
        index,
        " exceeds totalSize ",
        totalSize_);

    ValueT value(std::forward<Args>(args)...);
    if (value == defaultValue_) {
      return;
    }

    if (isDense_) {
      if (dense_.nonDefaultBits.set(index)) {
        dense_.values[index] = std::move(value);
      }
      return;
    }

    sparse_.emplace(key, std::move(value));
  }

  [[nodiscard]] FOLLY_ALWAYS_INLINE ValueT getValue(KeyT key) const noexcept {
    const auto index = static_cast<std::size_t>(key);
    FOLLY_SAFE_CHECK(
        index < totalSize_,
        "CompressedIdMap::getValue: key index ",
        index,
        " exceeds totalSize ",
        totalSize_);

    return isDense_ ? dense_.values[index]
                    : folly::get_default(sparse_, key, defaultValue_);
  }

  [[nodiscard]] ValueT getDefaultValue() const noexcept {
    return defaultValue_;
  }

  [[nodiscard]] bool isDense() const noexcept {
    return isDense_;
  }

  [[nodiscard]] std::size_t totalSize() const noexcept {
    return totalSize_;
  }

  [[nodiscard]] std::size_t nonDefaultSize() const noexcept {
    return isDense_ ? dense_.nonDefaultBits.numSetBits() : sparse_.size();
  }

  template <typename F>
  void forEachNonDefault(F&& fn) const {
    if (isDense_) {
      const auto* data = dense_.values.data();
      dense_.nonDefaultBits.forEachSetBit(
          [&](std::size_t i) { fn(KeyT(i), data[i]); });
      return;
    }

    for (const auto& [key, value] : sparse_) {
      fn(key, value);
    }
  }

 private:
  bool isDense_{false};
  std::size_t totalSize_{0};
  ValueT defaultValue_;
  SparseStorage sparse_;
  DenseStorage dense_;
};

} // namespace facebook::algopt
