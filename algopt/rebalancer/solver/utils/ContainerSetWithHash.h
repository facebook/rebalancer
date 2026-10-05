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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <memory>
#include <optional>
#include <stddef.h>
#include <stdint.h>

namespace facebook::rebalancer {

class ContainerSetWithHash {
 public:
  ContainerSetWithHash();

  void set(
      std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr);

  int64_t getHashValue() const;

  std::shared_ptr<const PackerSet<entities::ContainerId>> getSetPtr() const;

  const PackerSet<entities::ContainerId>& getNonNullSet() const;

  bool exists() const;

  // return true if containersPtr_ is an empty set; throws if containerPtr_ is
  // nullptr
  bool isEmpty() const;

  // return std::nullopt if containersPtr_ is nullptr, else return the size of
  // *containersPtr_
  std::optional<size_t> size() const;

 private:
  std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr_ =
      nullptr;
  std::optional<int64_t> hashValueOpt_ = std::nullopt;
};

} // namespace facebook::rebalancer
