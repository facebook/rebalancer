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

@hack.NamePrefix{prefix = "rebalancer_packer_"}
@hack.LegacyOmitPrefixInNameString
package "meta.com/algopt/rebalancer"

namespace hack ""

namespace cpp2 facebook.rebalancer
namespace py3 rebalancer.packer

include "algopt/rebalancer/interface/thrift/SolverSpecs.thrift"
include "thrift/annotation/hack.thrift"

enum SolverType {
  LOCAL_SEARCH = 0,
  OPTIMAL = 1,
  OPTIMAL_SUBSET = 2,
  SIMULATED_ANNEALING = 3,
  INTERACTIVE = 4,
  LOCAL_SEARCH_STAGES = 6,
}

union SolverT {
  // 11-16: deprecated
  17: SolverSpecs.OptimalSolverSpec optimalSolverSpec;
  18: SolverSpecs.OptimalSubsetSolverSpec optimalSubsetSolverSpec;
  19: SolverSpecs.LocalSearchSolverSpec localSearchSolverSpec;
  20: SolverSpecs.LocalSearchStageSolverSpec localSearchStageSolverSpec;
  21: SolverSpecs.RasHybridSolverSpec rasHybridSolverSpec;
}

struct StrategyT {
  1: list<SolverT> solvers;
}

struct ExpressionPropertyValueDouble {
  1: double value;
}

struct ExpressionPropertyValueString {
  1: string value;
}

struct ExpressionPropertyValueContainerIdList {
  1: list<i32> value;
}

struct ExpressionPropertyValueInt {
  1: i32 value;
}

struct ExpressionPropertyValueBool {
  1: bool value;
}

struct ExpressionPropertyValueObjectIdDoubleMap {
  1: map<i32, double> value;
}

struct Point2d {
  1: double x;
  2: double y;
}

struct ExpressionPropertyValuePoint2dList {
  1: list<Point2d> value;
}

struct ExpressionPropertyValueObjectId {
  1: i32 value;
}

struct ExpressionPropertyValueContainerId {
  1: i32 value;
}

struct ExpressionPropertyValueContainerNameList {
  1: list<string> value;
}

struct ExpressionPropertyValueObjectNameDoubleMap {
  1: map<string, double> value;
}

struct ExpressionPropertyValueObjectName {
  1: string value;
}

struct ExpressionPropertyValueContainerName {
  1: string value;
}

union ExpressionPropertyValue {
  1: ExpressionPropertyValueDouble valueDouble;
  2: ExpressionPropertyValueString valueString;
  3: ExpressionPropertyValueContainerIdList valueContainerIdList;
  4: ExpressionPropertyValueInt valueInt;
  5: ExpressionPropertyValueBool valueBool;
  6: ExpressionPropertyValueObjectIdDoubleMap valueObjectIdDoubleMap;
  7: ExpressionPropertyValuePoint2dList valuePoint2dList;
  8: ExpressionPropertyValueObjectId valueObjectId;
  9: ExpressionPropertyValueContainerId valueContainerId;
  10: ExpressionPropertyValueContainerNameList valueContainerNameList;
  11: ExpressionPropertyValueObjectNameDoubleMap valueObjectNameDoubleMap;
  12: ExpressionPropertyValueObjectName valueObjectName;
  13: ExpressionPropertyValueContainerName valueContainerName;
}

struct ExpressionProperties {
  1: map<string, ExpressionPropertyValue> properties;
}
