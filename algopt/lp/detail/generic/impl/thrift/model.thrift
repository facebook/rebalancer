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

package "meta.com/algopt/rebalancer"

namespace cpp2 facebook.algopt.lp.thrift
namespace hack AlgoptLp
namespace py3 algopt.lp.thrift

include "thrift/annotation/hack.thrift"

@hack.Attributes{
  attributes = [
    "
  \JSEnum(shape('name' => 'AlgoptLpVariableType', 'flow_enum' => true))",
    "\GraphQLEnum('XFBAlgoptLpVariableType')",
    "\GraphQLUnprefixedNamingScheme",
    "\SelfDescriptive",
    "\Oncalls('algopt')
",
  ],
}
enum VariableType {
  BINARY = 0,
  INTEGER = 1,
  CONTINUOUS = 2,
  SEMI_CONTINUOUS = 3,
  SEMI_INTEGER = 4,
}

@hack.Attributes{
  attributes = [
    "
  \JSEnum(shape('name' => 'AlgoptLpConstraintType', 'flow_enum' => true))",
    "\GraphQLEnum('XFBAlgoptLpConstraintType')",
    "\GraphQLUnprefixedNamingScheme",
    "\SelfDescriptive",
    "\Oncalls('algopt')
",
  ],
}
enum ConstraintType {
  EQUALS_ZERO = 0,
  GEQ_ZERO = 1,
  LEQ_ZERO = 2,
}

@hack.MigrationBlockingAllowInheritance
struct GenericVariable {
  1: string name;
  2: VariableType type;
  3: optional double lowerBound;
  4: optional double upperBound;
  5: optional i32 partitionId;
  6: optional double initialValue;
  7: optional double threshold;
}

@hack.MigrationBlockingAllowInheritance
struct GenericExpression {
  1: map<i64, double> linearCoeffs;
  2: map<i64, map<i64, double>> quadraticCoeffs;
  3: double constant;
}

@hack.MigrationBlockingAllowInheritance
struct GenericConstraint {
  1: GenericExpression expr;
  3: ConstraintType type;
  4: string name;
}

@hack.MigrationBlockingAllowInheritance
struct GenericProblem {
  1: list<GenericVariable> variables;
  2: list<GenericConstraint> constraints;
  3: list<GenericExpression> objectives;
}
