/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * TypeScript types matching the Thrift IDL definitions for the
 * RebalancerExplorer BFF (SimpleJSON wire format).
 *
 * Source: fbcode/rebalancer/explorer/if/explorer.thrift
 */

export interface Handle {
  manifoldId: string;
  host: string;
  port: number;
  taskId: number;
}

export interface HandleRequest {
  manifoldId: string;
  // Idle TTL in seconds. Non-positive values use the 1-hour default; huge
  // values are clamped. getHandle only raises an existing sandbox's TTL.
  ttlSeconds?: number;
  // Caller label for logs; empty when unset.
  clientId?: string;
}

export interface HandleResponse {
  handle: Handle;
}

export interface BundleExpirationResponse {
  expiresAt: number;
}

export enum SandboxStatus {
  NOT_LOADED = 1,
  LOADING = 2,
  LOADED = 3,
}

export interface SandboxStatusResponse {
  status: SandboxStatus;
}

export interface ServerStatus {
  loadingSandboxCount: number;
  loadedSandboxCount: number;
  freeMemoryBytes: number;
  usedMemoryBytes: number;
}

export interface ProblemMetadata {
  objectName: string;
  containerName: string;
  scopeNames: string[];
  variableName: string;
  hasFinalAssignment: boolean;
  runId: string;
  hasIntermediateAssignment: boolean;
  numSteps?: number;
  partitionNames: string[];
  dynamicDimensionNames: string[];
  metricCollectionNames: string[];
  objectiveNames: string[];
  canDisplayObjChangesInMoveSetsTable: boolean;
  hasOnlySingleMoves: boolean;
  serviceName: string;
  serviceScope: string;
  solverType: string;
  solverEndReason: string;
  totalRuntime: number;
  numObjects: number;
  numContainers: number;
  numDimensions: number;
  numScopes: number;
}

export interface ProblemMetadataResponse {
  metadata: ProblemMetadata;
}

////
// Common
////

export enum Comparator {
  EQ = 0,
  NE = 1,
  LT = 2,
  LE = 3,
  GT = 4,
  GE = 5,
}

////
// Filtering
////

export interface FilterRuleStringAny {
  column: string;
  values: string[];
}

export interface FilterRuleRegex {
  column: string;
  regex: string;
}

export interface FilterRuleNumeric {
  column: string;
  comparator: Comparator;
  doubleValue: number;
}

export interface FilterRuleStringNe {
  column: string;
  value: string;
}

export type FilterRule =
  | {regex: FilterRuleRegex}
  | {numeric: FilterRuleNumeric}
  | {stringAny: FilterRuleStringAny}
  | {stringNe: FilterRuleStringNe};

export interface Filter {
  rules: FilterRule[];
}

////
// Pagination
////

export interface Page {
  offset: number;
  limit: number;
}

////
// Ordering
////

export enum OrderDirection {
  ASCENDING = 0,
  DESCENDING = 1,
}

export interface OrderColumn {
  name: string;
  direction: OrderDirection;
}

export interface Order {
  columns: OrderColumn[];
}

export interface Group {
  columns: string[];
}

////
// Query
////

export interface Query {
  entity: string;
  filter?: Filter;
  order?: Order;
  page?: Page;
  group?: Group;
}

////
// Result
////

export enum ColumnType {
  ENTITY_NAME = 2,
  DIMENSION = 3,
  PARTITION = 4,
  ASSIGNMENT = 5,
  SCOPE = 6,
  UTILIZATION = 7,
  DOUBLE = 9,
  STRING = 10,
  IDENTIFIER = 11,
  INTEGER = 12,
}

export interface ColumnDescription {
  name: string;
  type: ColumnType;
  primaryKey: boolean;
  description: string;
}

export interface CellData {
  stringValue?: string;
  doubleValue?: number;
}

export interface RowData {
  cells: CellData[];
}

export interface Result {
  columns: ColumnDescription[];
  rows: RowData[];
  totalCount: number;
}

////
// Data request/response
////

export interface DataRequest {
  query: Query;
}

export interface DataResponse {
  result: Result;
}

////
// Metric distribution
////

export interface MetricDistributionRequest {
  entity: string;
  metric: string;
  maxPoints: number;
}

export interface MetricDistributionPoint {
  index: number;
  metricValue: number;
}

export interface MetricDistributionResponse {
  points: MetricDistributionPoint[];
}

////
// Typeahead
////

export interface TypeaheadRequest {
  entity: string;
  query: string;
  limit: number;
}

export interface TypeaheadResponse {
  matches: string[];
}

////
// Search
////

export interface Search {
  query?: string;
}

////
// Assignment
////

export enum AssignmentBase {
  INITIAL = 0,
  FINAL = 1,
  INTERMEDIATE = 2,
}

export interface Assignment {
  base: AssignmentBase;
  variableToContainerOverride: Record<string, string>;
  searchStep?: number;
}

////
// Evaluation
////

export enum ExpressionType {
  OBJECTIVE = 0,
  CONSTRAINT = 1,
}

export interface ExpressionResult {
  id: number;
  type: ExpressionType;
  name: string;
  description: string;
  value: number;
  tupleIndex: number;
}

export interface EvaluationResult {
  expressions: ExpressionResult[];
}

export interface EvaluateRequest {
  assignment: Assignment;
}

export interface EvaluateResponse {
  result: EvaluationResult;
}

////
// Moves between assignments
////

export interface MovesBetweenAssignmentsRequest {
  source: Assignment;
  destination: Assignment;
}

export interface MovesBetweenAssignmentsResponse {
  variableToContainer: Record<string, string>;
}

////
// Move sets
////

export interface MoveSetsRequest {
  assignmentA: Assignment;
  assignmentB: Assignment;
  partitionName?: string;
  scopeName?: string;
  query: Query;
  objectiveNames?: string[];
}

export interface MoveSetsResponse {
  table: Result;
}

////
// Tree nodes
////

export enum TreeNodeOrderMetric {
  SOURCE_VALUE = 0,
  DESTINATION_VALUE = 1,
  DELTA_VALUE = 2,
}

export interface Point2d {
  x: number;
  y: number;
}

export type ExpressionPropertyValue =
  | {valueDouble: {value: number}}
  | {valueString: {value: string}}
  | {valueInt: {value: number}}
  | {valueBool: {value: boolean}}
  | {valuePoint2dList: {value: Point2d[]}}
  | {valueContainerNameList: {value: string[]}}
  | {valueObjectNameDoubleMap: {value: Record<string, number>}}
  | {valueObjectName: {value: string}}
  | {valueContainerName: {value: string}};

export interface ExpressionProperty {
  name: string;
  value: ExpressionPropertyValue;
}

export interface TreeNodeRequest {
  expressionId: number;
  sourceAssignment: Assignment;
  destinationAssignment: Assignment;
  childrenPage: Page;
  childrenOrderMetric: TreeNodeOrderMetric;
  childrenOrderDirection: OrderDirection;
  search?: Search;
}

export interface TreeNode {
  expressionId: number;
  expressionType: string;
  name: string;
  description: string;
  sourceValue: number;
  destinationValue: number;
  coefficient: number;
  properties?: ExpressionProperty[];
}

export interface TreeNodeResponse {
  node: TreeNode;
  children: TreeNode[];
}

////
// Specs
////

export interface GoalSpec {
  name: string;
  weight: number;
  tupleIndex: number;
  specJson: string;
  id: number;
}

export interface GoalSpecRequest {
  name: string;
}

export interface GoalSpecResponse {
  spec: GoalSpec;
}

export interface ConstraintSpec {
  name: string;
  invalidCost: number;
  invalidState: number;
  specJson: string;
  id: number;
}

export interface ConstraintSpecRequest {
  name: string;
}

export interface ConstraintSpecResponse {
  spec: ConstraintSpec;
}

////
// Local search profiling
////

export interface MoveTypeEvent {
  moveTypeIndex: number;
  duration: number;
  initialValue: number;
  finalValue: number;
}

export interface LocalSearchProfile {
  moveTypeNames: string[];
  moveTypeEvents: MoveTypeEvent[];
}

export interface LocalSearchProfilesResponse {
  profiles: LocalSearchProfile[];
}
