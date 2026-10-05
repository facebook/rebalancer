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
#include "algopt/rebalancer/entities/Map.h"

#include <memory>
#include <utility>

namespace facebook::rebalancer::packer::tests {

inline entities::ObjectId object(int i) {
  return entities::ObjectId(i);
}

inline entities::ContainerId container(int i) {
  return entities::ContainerId(i);
}

inline entities::GroupId group(int i) {
  return entities::GroupId(i);
}

template <typename T = entities::ObjectId>
std::shared_ptr<const entities::Map<T, double>> makeSharedPtrEntityToValueMap(
    entities::Map<T, double>&& map) {
  return std::make_shared<const entities::Map<T, double>>(std::move(map));
}

} // namespace facebook::rebalancer::packer::tests
