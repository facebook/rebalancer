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

#include "algopt/rebalancer/solver/utils/Change.h"

#include "algopt/rebalancer/entities/Identifiers.h"

#include <fmt/core.h>

#include <sstream>
#include <stdexcept>

namespace facebook::rebalancer {

Change::Change(
    entities::ObjectId object,
    entities::ContainerId container,
    int value)
    : object(object), container(container), value(value) {
  if (value != 1 && value != -1) {
    throw std::runtime_error(fmt::format("invalid change value {}", value));
  }
}

entities::ObjectId Change::getObject() const {
  return object;
}

entities::ContainerId Change::getContainer() const {
  return container;
}

int Change::getValue() const {
  return value;
}

std::string Change::toString() const {
  std::stringstream ss;
  ss << "Object = " << object << ", Container = " << container
     << ", Value = " << value;
  return ss.str();
}

} // namespace facebook::rebalancer
