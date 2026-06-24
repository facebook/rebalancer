// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <string>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace explorer {

class FilterModel {
  /* Applies filter on model. */
 private:
  /* Ensure object cannot be created for this class. */
  explicit FilterModel();

 public:
  // filter model
  static Table applyFilter(const Filter& filter, Table table);
};

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
