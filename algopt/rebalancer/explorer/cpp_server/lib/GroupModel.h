// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/Hash.h>

#include <string>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace explorer {

struct GroupValueCellStruct {
  std::vector<std::string> groupCellValue;

  bool operator==(const GroupValueCellStruct& other) const {
    return std::equal(
        groupCellValue.begin(),
        groupCellValue.end(),
        other.groupCellValue.begin(),
        other.groupCellValue.end());
  }
};

class GroupModel {
  /* Applies grouping on model. */
 private:
  /* Ensure object cannot be created for this class. */
  explicit GroupModel();

 public:
  // group model
  static Table applyGroup(const Group& group, Table table);
};

} // namespace explorer
} // namespace rebalancer
} // namespace facebook

namespace std {
template <>
struct hash<facebook::rebalancer::explorer::GroupValueCellStruct> {
  std::size_t operator()(
      const facebook::rebalancer::explorer::GroupValueCellStruct&
          groupCellStruct) const noexcept {
    return folly::hash::hash_range(
        groupCellStruct.groupCellValue.begin(),
        groupCellStruct.groupCellValue.end());
  }
};

} // namespace std
