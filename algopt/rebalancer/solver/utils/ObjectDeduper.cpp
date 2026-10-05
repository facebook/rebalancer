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

#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
namespace facebook::rebalancer {

ObjectDeduper::ObjectDeduper(
    const EquivalenceSets* equivalenceSets,
    const ObjectStore& objects)
    : GenericObjectDeduper<ObjectStore::Iterator>(
          equivalenceSets,
          objects.begin(),
          objects.end()) {}

} // namespace facebook::rebalancer
