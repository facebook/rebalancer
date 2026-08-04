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
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookupDynamic.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/StableStayed.h"
#include "algopt/rebalancer/solver/expressions/Swaps.h"
#include "algopt/rebalancer/solver/expressions/Transform.h"

#include <memory>
#include <vector>

namespace facebook::rebalancer {

ExprPtr const_expr(double value, const entities::Universe& universe);

ExprPtr operator+(ExprPtr lhs, ExprPtr rhs);
ExprPtr operator+(ExprPtr lhs, double rhs);
ExprPtr operator+(double lhs, ExprPtr rhs);
void operator+=(ExprPtr& lhs, ExprPtr rhs);
void operator+=(ExprPtr& lhs, double rhs);
void operator+=(ExprPtr& lhs, const std::vector<ExprPtr>& exprs);

ExprPtr operator-(ExprPtr lhs, ExprPtr rhs);
ExprPtr operator-(ExprPtr lhs, double rhs);
ExprPtr operator-(double lhs, ExprPtr rhs);
void operator-=(ExprPtr& lhs, ExprPtr rhs);
void operator-=(ExprPtr& lhs, double rhs);

ExprPtr operator*(ExprPtr lhs, double rhs);
ExprPtr operator*(double lhs, ExprPtr rhs);
void operator*=(ExprPtr& lhs, double rhs);

ExprPtr operator/(ExprPtr lhs, double rhs);
void operator/=(ExprPtr& lhs, double rhs);
bool operator==(ExprPtr lhs, double rhs);

/* binary_min and binary_max are optimization APIs
 * it has binary specific optimizations.
 * NOTE: See ExpressionBuilder for a cached version of binary_min, which can
 * result in a large reduction in materialization time due to not creating extra
 * expressions
 */
ExprPtr binary_min(ExprPtr lhs, ExprPtr rhs);
void inplace_binary_max(ExprPtr& lhs, ExprPtr rhs);
ExprPtr min(ExprPtr lhs, ExprPtr rhs);
ExprPtr min(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe);
ExprPtr max(ExprPtr lhs, ExprPtr rhs);
ExprPtr max(ExprPtr lhs, double rhs);
ExprPtr max(double lhs, ExprPtr rhs);
ExprPtr max(
    std::initializer_list<ExprPtr> exprs,
    const entities::Universe& universe);
ExprPtr max(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe);
void inplace_max(ExprPtr& lhs, ExprPtr rhs);

ExprPtr any_positive(
    std::initializer_list<ExprPtr> exprs,
    const entities::Universe& universe);
ExprPtr any_positive(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe);

ExprPtr any_positive(
    std::initializer_list<ExprPtr> exprs,
    const entities::Universe& universe,
    const double feasibilityTolerance);
ExprPtr any_positive(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe,
    const double feasibilityTolerance);
void inplace_any_positive(ExprPtr& lhs, ExprPtr rhs);
void any_positive_add(ExprPtr& lhs, ExprPtr rhs);

std::vector<ExprPtr> equals(ExprPtr lhs, ExprPtr rhs);
std::vector<ExprPtr> equals(ExprPtr lhs, double rhs);
std::vector<ExprPtr> equals(double lhs, ExprPtr rhs);
/* add rhs as a single node to lhs, do not merge */
void inplace_add(ExprPtr& lhs, ExprPtr rhs, double coef = 1);

ExprPtr swaps(
    const PackerMap<entities::ObjectId, entities::ContainerId>&
        initial_assignment,
    const entities::Universe& universe,
    const folly::Optional<PackerSet<entities::ObjectId>>& subset = {},
    Swaps::SubsetDefinition subsetDefinition =
        Swaps::SubsetDefinition::AT_LEAST_ONE_IN_SUBSET);

ExprPtr bipartite_swaps(
    PackerMap<entities::ObjectId, entities::ContainerId> initial_assignment,
    PackerSet<entities::ContainerId> left_subset,
    PackerSet<entities::ContainerId> right_subset,
    const entities::Universe& universe);

std::shared_ptr<ObjectLookup> object_lookup(
    ExprPtr obj_vec,
    std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr,
    const Assignment& initialAssignment);

std::shared_ptr<ObjectLookupDynamic> object_lookup_dynamic(
    ExprPtr sumOfObjectLookups,
    const entities::ObjectScalarDimension& dimension);

std::shared_ptr<StableStayed> stable_stayed(
    std::shared_ptr<ObjectVector> initialObjectVector,
    std::shared_ptr<ObjectVector> fullObjectVector,
    std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr,
    const entities::Universe& universe,
    const Assignment& initialAssignment);

ExprPtr object_partition(
    std::shared_ptr<const PartitionInfo> partitionInfo,
    entities::DimensionId dimensionId,
    PackerMap<entities::GroupId, double> groupLimits,
    std::optional<PackerSet<entities::ScopeItemId>> scopeItemIds = std::nullopt,
    PackerMap<entities::GroupId, double> groupCoefficients = {},
    double defaultGroupLimit = 0.0,
    double defaultGroupCoefficient = 1.0);

ExprPtr object_partition_lookup(
    ExprPtr objectPartition,
    std::shared_ptr<const PackerSet<entities::ContainerId>> lookupContainers,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    const Assignment& initialAssignment,
    PackerMap<entities::GroupId, double> groupLimitOverrides = {},
    PackerSet<entities::ObjectId> initialDuringObjects = {},
    std::optional<double> defaultGroupLimitOverride = std::nullopt,
    ObjectPartitionLookupPenaltyTransform penaltyTransform =
        ObjectPartitionLookupPenaltyTransform::IDENTITY,
    int groupsAllowed = 0,
    bool minBound = false);

std::shared_ptr<ObjectVector> object_vector(
    entities::ObjectValues objectValues,
    const entities::Universe& universe);

std::shared_ptr<ObjectVector> object_vector(
    std::shared_ptr<const entities::ObjectIdToDoubleMap>
        objectToNonDefaultValue,
    const entities::Universe& universe);

ExprPtr power(ExprPtr base, double exponent);
ExprPtr product(ExprPtr lhs, ExprPtr rhs);
ExprPtr quotient(ExprPtr lhs, ExprPtr rhs);
ExprPtr square(ExprPtr expr);
ExprPtr square(ExprPtr expr, const ApproximationHint& hint);
ExprPtr step(ExprPtr expr);
ExprPtr step_mod_k(ExprPtr expr, int k);
ExprPtr ceil(ExprPtr expr);
ExprPtr floor(ExprPtr expr);
ExprPtr log(ExprPtr expr);
ExprPtr rectangle(ExprPtr expr, const double lb, const double ub);
ExprPtr sum_over_threshold(
    ExprPtr threshold,
    const std::vector<ExprPtr>& values,
    bool square);

ExprPtr variable(
    entities::ObjectId obj,
    entities::ContainerId con,
    const entities::Universe& universe,
    const Assignment& initialAssignment);

ExprPtr piecewise(
    const std::vector<std::pair<double, double>>& points,
    ExprPtr x,
    bool continuous = true);
ExprPtr nth_largest(
    const std::vector<ExprPtr>& values,
    int n,
    const entities::Universe& universe);

ExprPtr boundsOverride(
    ExprPtr expr,
    std::optional<double> lb,
    std::optional<double> ub);
} // namespace facebook::rebalancer
