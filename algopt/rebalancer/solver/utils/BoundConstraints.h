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

#include <folly/Optional.h>

#include <memory>
#include <optional>
#include <utility>

namespace facebook::rebalancer {

class BoundConstraints {
 public:
  class Builder;

 public:
  // default constructor, builds an empty bound constraints object
  BoundConstraints()
      : dynamic_(std::nullopt),
        notGiving_(std::nullopt),
        notTaking_(std::nullopt) {}

  bool giving(entities::ContainerId container) const;
  bool anyGiving(const PackerSet<entities::ContainerId>& containers) const;

  bool taking(entities::ContainerId container) const;
  bool anyTaking(const PackerSet<entities::ContainerId>& containers) const;

  bool isEmpty() const;

  void throwIfEmpty() const;

 private:
  BoundConstraints(
      std::optional<PackerSet<entities::ContainerId>> dynamic,
      std::optional<PackerSet<entities::ContainerId>> notGiving,
      std::optional<PackerSet<entities::ContainerId>> notTaking)
      : dynamic_(std::move(dynamic)),
        notGiving_(std::move(notGiving)),
        notTaking_(std::move(notTaking)) {}

 private:
  bool inDynamicContainerSet(entities::ContainerId container) const;
  bool anyInDynamicContainerSet(
      const PackerSet<entities::ContainerId>& containers) const;

 private:
  std::optional<PackerSet<entities::ContainerId>> dynamic_;
  std::optional<PackerSet<entities::ContainerId>> notGiving_;
  std::optional<PackerSet<entities::ContainerId>> notTaking_;
};

class BoundConstraints::Builder {
 public:
  BoundConstraints build();

  // setters
  BoundConstraints::Builder& setDynamicContainers(
      PackerSet<entities::ContainerId> dynamic_containers);
  BoundConstraints::Builder& setNotGivingContainers(
      PackerSet<entities::ContainerId> not_giving_containers);
  BoundConstraints::Builder& setNotTakingContainers(
      PackerSet<entities::ContainerId> not_taking_containers);

 private:
  void checkNotBuilt() const;

 private:
  bool built_ = false;
  std::optional<PackerSet<entities::ContainerId>> dynamic_;
  std::optional<PackerSet<entities::ContainerId>> notGiving_;
  std::optional<PackerSet<entities::ContainerId>> notTaking_;
};

} // namespace facebook::rebalancer
