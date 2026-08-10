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

#include "algopt/rebalancer/solver/moves/Move.h"

namespace facebook::rebalancer {

class MoveSet {
 public:
  static MoveSet fromChangeSet(const ChangeSet& changes);

  MoveSet();

  void insert(Move move);
  void erase(std::vector<Move>::const_iterator move);
  void clear();

  bool empty() const;
  size_t size() const;
  const Move& at(size_t index) const;
  ChangeSet getChangeSet() const;
  std::string toString(const entities::Universe& universe) const;
  std::vector<Move>::const_iterator begin() const;
  std::vector<Move>::const_iterator end() const;
  static int compare(const MoveSet& lhs, const MoveSet& rhs);

 private:
  std::vector<Move> moves_;
};

} // namespace facebook::rebalancer
