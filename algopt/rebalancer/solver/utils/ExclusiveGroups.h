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

#include "algopt/rebalancer/solver/utils/Util.h"

#include <string>

namespace facebook::rebalancer {

PackerMap<std::string, std::string> computeExclusiveGroupsAssignment(
    const PackerMap<std::string, double>& groupSizes,
    const PackerMap<std::string, double>& scopeItemSizes,
    const PackerMap<std::string, PackerMap<std::string, double>>&
        groupScopeItemFootprint);

} // namespace facebook::rebalancer
