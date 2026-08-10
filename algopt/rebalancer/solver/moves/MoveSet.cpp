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

#include "algopt/rebalancer/solver/moves/MoveSet.h"

#include "algopt/rebalancer/solver/utils/Util.h"

#include <fmt/format.h>
#include <folly/container/irange.h>

#include <cassert>
#include <iterator>

namespace facebook::rebalancer {

MoveSet MoveSet::fromChangeSet(const ChangeSet& changes) {
  PackerMap<entities::ObjectId, entities::ContainerId> from, to;
  for (auto& change : changes) {
    if (change.getValue() == -1) {
      if (!from.insert(
                   std::make_pair(change.getObject(), change.getContainer()))
               .second) {
        throw std::runtime_error(
            fmt::format("object {} removed twice", change.getObject()));
      }
    } else {
      assert(change.getValue() == 1);
      if (!to.insert(std::make_pair(change.getObject(), change.getContainer()))
               .second) {
        throw std::runtime_error(
            fmt::format("object {} added twice", change.getObject()));
      }
    }
  }
  if (from.size() != to.size()) {
    throw std::runtime_error(
        "mismatch between number of objects added and removed");
  }
  MoveSet moves;
  for (auto& [object, fromContainer] : from) {
    auto it = to.find(object);
    if (it == to.end()) {
      throw std::runtime_error(
          fmt::format("object {} removed but not added", object));
    }
    auto toContainer = it->second;
    moves.insert(Move(object, fromContainer, toContainer));
  }
  return moves;
}

MoveSet::MoveSet() = default;

void MoveSet::insert(Move move) {
  moves_.push_back(std::move(move));
}

void MoveSet::erase(std::vector<Move>::const_iterator move) {
  moves_.erase(move);
}

void MoveSet::clear() {
  moves_.clear();
}

bool MoveSet::empty() const {
  return moves_.empty();
}

size_t MoveSet::size() const {
  return moves_.size();
}

const Move& MoveSet::at(size_t index) const {
  return moves_.at(index);
}

ChangeSet MoveSet::getChangeSet() const {
  ChangeSet changeSet;
  for (auto& move : moves_) {
    changeSet.insert(Change(move.getObject(), move.getSourceContainer(), -1));
    changeSet.insert(
        Change(move.getObject(), move.getDestinationContainer(), 1));
  }
  return changeSet;
}

std::string MoveSet::toString(const entities::Universe& universe) const {
  if (moves_.empty()) {
    return "[]";
  }

  fmt::memory_buffer buffer;
  fmt::format_to(std::back_inserter(buffer), "[");
  for (const auto& move : moves_) {
    fmt::format_to(
        std::back_inserter(buffer), "\n  {}", move.toString(universe));
  }
  fmt::format_to(std::back_inserter(buffer), "\n]");
  return fmt::to_string(buffer);
}

std::vector<Move>::const_iterator MoveSet::begin() const {
  return moves_.begin();
}

std::vector<Move>::const_iterator MoveSet::end() const {
  return moves_.end();
}

/* Static */
int MoveSet::compare(const MoveSet& lhs, const MoveSet& rhs) {
  if (lhs.size() < rhs.size()) {
    return 1;
  }
  if (lhs.size() > rhs.size()) {
    return -1;
  }
  for (const auto i : folly::irange(lhs.size())) {
    const int moveCompare = Move::compare(lhs.at(i), rhs.at(i));
    if (moveCompare != 0) {
      return moveCompare;
    }
  }
  return 0;
}

} // namespace facebook::rebalancer
