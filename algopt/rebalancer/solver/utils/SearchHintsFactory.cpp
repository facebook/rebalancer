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

#include "algopt/rebalancer/solver/utils/SearchHintsFactory.h"

#include "algopt/rebalancer/solver/moves/SingleColdestStratifiedMoveType.h"
#include "algopt/rebalancer/solver/utils/SearchHints.h"

#include <folly/Range.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

SearchHints SearchHintsFactory::create(
    const interface::LocalSearchSolverSpec& spec) {
  const int stratified_sample_size = *spec.stratifiedSampleSize();
  // This can be decentralized by adding methods to the interface of MoveType
  // such as needsColdestStratifiedContainers() which move types needing it
  // can override.
  const auto& moveTypeList = *spec.moveTypeList();
  auto byName = [&name = std::as_const(kSingleColdestStratifiedMoveTypeName)](
                    const interface::MoveTypeSpec& mts) {
    return mts.getType() == interface::MoveTypeSpec::Type::moveTypeName &&
        mts.get_moveTypeName() == name;
  };
  const bool coldest_stratified_containers =
      (std::ranges::find_if(moveTypeList, byName) != moveTypeList.end());
  SearchHintsConfig hints_config{
      .enable_coldest_stratified_containers = coldest_stratified_containers,
      .stratified_sample_size = stratified_sample_size};
  return SearchHints(std::move(hints_config));
}

} // namespace facebook::rebalancer
