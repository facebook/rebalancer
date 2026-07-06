include "thrift/annotation/thrift.thrift"
include "algopt/rebalancer/interface/thrift/Types.thrift"
include "algopt/rebalancer/solver/if/packer.thrift"
include "thrift/annotation/hack.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 facebook.rebalancer.explorer
namespace py3 rebalancer.explorer
namespace php rebalancer_explorer

////
// Common
////
@hack.Attributes{
  attributes = [
    "\JSEnum(shape('name' => 'RebalancerExplorerComparator', 'flow_enum' => false))",
    "\GraphQLEnum('XFBRebalancerExplorerComparator')",
    "\GraphQLUnprefixedNamingScheme",
    "\SelfDescriptive",
    "\Oncalls('algopt')
",
  ],
}
enum Comparator {
  EQ = 0, // equal
  NE = 1, // not equal
  LT = 2, // less than
  LE = 3, // less or equal
  GT = 4, // greater than
  GE = 5, // greater or equal
}

////
// Filtering
////

// Matches a column's value against a set of strings
@hack.MigrationBlockingAllowInheritance
struct FilterRuleStringAny {
  1: string column;
  2: list<string> values;
}

// Matches a column's value against a regex
@hack.MigrationBlockingAllowInheritance
struct FilterRuleRegex {
  1: string column;
  2: string regex;
}

// Compares a column's value against a given value
@thrift.ReserveIds{ids = [3]}
@hack.MigrationBlockingAllowInheritance
struct FilterRuleNumeric {
  1: string column;
  2: Comparator comparator;
  4: double doubleValue;
}

// Matches a column's value if different than the one provided
@hack.MigrationBlockingAllowInheritance
struct FilterRuleStringNe {
  1: string column;
  2: string value;
}

// Union of all filter rule types
@hack.MigrationBlockingAllowInheritance
union FilterRule {
  1: FilterRuleRegex regex;
  2: FilterRuleNumeric numeric;
  3: FilterRuleStringAny stringAny;
  4: FilterRuleStringNe stringNe;
}

// Filter as a set of rules
@hack.MigrationBlockingAllowInheritance
struct Filter {
  1: list<FilterRule> rules; // combined with AND
}

////
// Pagination
////

@hack.MigrationBlockingAllowInheritance
struct Page {
  1: i32 offset; // 0-based index
  2: i32 limit;
}

////
// Ordering
////
@hack.Attributes{
  attributes = [
    "\JSEnum(shape('name' => 'RebalancerExplorerOrderDirection', 'flow_enum' => false))",
    "\GraphQLEnum('XFBRebalancerExplorerOrderDirection')",
    "\GraphQLUnprefixedNamingScheme",
    "\SelfDescriptive",
    "\Oncalls('algopt')",
  ],
}
enum OrderDirection {
  ASCENDING = 0,
  DESCENDING = 1,
}

@hack.MigrationBlockingAllowInheritance
struct OrderColumn {
  1: string name;
  2: OrderDirection direction;
}

@hack.MigrationBlockingAllowInheritance
struct Order {
  1: list<OrderColumn> columns;
}

@hack.MigrationBlockingAllowInheritance
struct Group {
  1: list<string> columns;
}

////
// Query
////

@hack.MigrationBlockingAllowInheritance
struct Query {
  1: string entity; // name of the object/scope/metricCollection table
  2: optional Filter filter;
  3: optional Order order;
  4: optional Page page;
  5: optional Group group;
}

////
// Search
////
struct Search {
  1: optional string query;
}

////
// Result
////

@hack.Attributes{
  attributes = [
    "JSEnum(shape('flow_enum' => false))",
    "GraphQLLegacyNamingScheme",
    "GraphQLEnum('RebalancerExplorerColumnType')",
    "SelfDescriptive",
  ],
}
enum ColumnType {
  ENTITY_NAME = 2,
  DIMENSION = 3,
  PARTITION = 4,
  ASSIGNMENT = 5,
  SCOPE = 6,
  UTILIZATION = 7,
  // 8 is deprecated
  DOUBLE = 9,
  STRING = 10,
  IDENTIFIER = 11,
  // use integer type for count-like columns or boolean
  INTEGER = 12,
}

struct ColumnDescription {
  1: string name;
  2: ColumnType type;
  3: bool primaryKey;
  // description of the column, which will, for example, be shown as a tooltip if non-empty
  4: string description;
}

@thrift.ReserveIds{ids = [1]}
struct CellData {
  // 1: deprecated
  2: optional string stringValue;
  3: optional double doubleValue;
}

struct RowData {
  1: list<CellData> cells;
}

struct Result {
  1: list<ColumnDescription> columns; // metadata
  2: list<RowData> rows; // data
  3: i32 totalCount; // total rows in the dataset
}

////
// Problem
////

struct ProblemMetadata {
  1: string objectName;
  2: string containerName;
  3: list<string> scopeNames;
  4: string variableName;
  5: bool hasFinalAssignment;
  6: string runId;
  7: bool hasIntermediateAssignment;
  8: optional i64 numSteps;
  9: list<string> partitionNames;
  10: list<string> dynamicDimensionNames;
  11: list<string> metricCollectionNames;
  12: list<string> objectiveNames;
  13: bool canDisplayObjChangesInMoveSetsTable;
  14: bool hasOnlySingleMoves;
  15: string serviceName;
  16: string serviceScope;
  17: string solverType;
  18: string solverEndReason;
  19: double totalRuntime;
  20: i64 numObjects;
  21: i64 numContainers;
  22: i64 numDimensions;
  23: i64 numScopes;
  24: string objectNamesFingerprint;
  25: string containerNamesFingerprint;
}

////
// Assignment
////

@hack.Attributes{
  attributes = [
    "JSEnum(shape('flow_enum' => false))",
    "GraphQLLegacyNamingScheme",
    "GraphQLEnum('RebalancerExplorerAssignmentBase')",
    "SelfDescriptive",
  ],
}
enum AssignmentBase {
  INITIAL = 0,
  FINAL = 1,
  INTERMEDIATE = 2,
}

struct Assignment {
  1: AssignmentBase base;
  2: map<string, string> variableToContainerOverride;
  3: optional i64 searchStep;
}

////
// Evaluation result
////

@hack.Attributes{
  attributes = [
    "JSEnum(shape('flow_enum' => false))",
    "GraphQLLegacyNamingScheme",
    "GraphQLEnum('RebalancerExplorerExpressionType')",
    "SelfDescriptive",
  ],
}
enum ExpressionType {
  OBJECTIVE = 0,
  CONSTRAINT = 1,
}

struct ExpressionResult {
  1: i64 id;
  2: ExpressionType type;
  3: string name;
  4: string description;
  5: double value;
  6: i32 tupleIndex;
}

struct EvaluationResult {
  1: list<ExpressionResult> expressions;
}

////
// Typeahead
////

struct TypeaheadRequest {
  1: string entity;
  2: string query;
  3: i32 limit;
}

struct TypeaheadResponse {
  1: list<string> matches;
}

////
// Moves between assignments
////

struct MovesBetweenAssignmentsRequest {
  1: Assignment source;
  2: Assignment destination;
}

struct MovesBetweenAssignmentsResponse {
  1: map<string, string> variableToContainer;
}

////
// Tree nodes
////

@hack.Attributes{
  attributes = [
    "JSEnum(shape('flow_enum' => false))",
    "GraphQLLegacyNamingScheme",
    "GraphQLEnum('RebalancerExplorerTreeNodeOrderMetric')",
    "SelfDescriptive",
  ],
}
enum TreeNodeOrderMetric {
  SOURCE_VALUE = 0,
  DESTINATION_VALUE = 1,
  DELTA_VALUE = 2,
}

struct TreeNodeRequest {
  1: i64 expressionId;
  2: Assignment sourceAssignment;
  3: Assignment destinationAssignment;
  4: Page childrenPage;
  5: TreeNodeOrderMetric childrenOrderMetric;
  6: OrderDirection childrenOrderDirection;
  7: Search search;
}

struct TreeNode {
  1: i64 expressionId;
  2: string expressionType;
  3: string name;
  4: string description;
  5: double sourceValue;
  6: double destinationValue;
  7: double coefficient;
  8: packer.ExpressionProperties properties;
}

struct TreeNodeResponse {
  1: TreeNode node;
  2: list<TreeNode> children;
}

////
// Metric distribution
////

struct MetricDistributionRequest {
  1: string entity;
  2: string metric;
  3: i32 maxPoints;
}

struct MetricDistributionPoint {
  1: i32 index;
  2: double metricValue;
}

struct MetricDistributionResponse {
  1: list<MetricDistributionPoint> points;
}

////
// Fetching Spec
////
@hack.MigrationBlockingAllowInheritance
struct GoalSpec {
  1: string name;
  2: double weight;
  3: i32 tupleIndex;
  4: string specJson;
  5: i64 id;
}

@hack.MigrationBlockingAllowInheritance
struct ConstraintSpec {
  1: string name;
  2: double invalidCost;
  3: double invalidState;
  4: Types.ConstraintPolicy policy;
  5: string specJson;
  6: i64 id;
}

struct MoveSetsRequest {
  1: Assignment assignmentA;
  2: Assignment assignmentB;
  3: optional string partitionName;
  4: optional string scopeName;
  5: Query query;
  // if specified, return extra columns in the result that has change w.r.t. each objective as as result of each moveSet
  6: optional list<string> objectiveNames;
}

struct MoveSetsResponse {
  1: Result table;
}

////
// Edit Problem
////

struct DeleteEditProblemRequest {
  // constraints (names) to be deleted from problem
  1: list<string> constraints;
  // goals (names) to be deleted from problem
  2: list<string> goals;
}

struct EditProblemRequest {
  // what to delete from problem
  1: DeleteEditProblemRequest toDelete;
}

struct EditProblemResponse {
  // manifold id where the edited problem is saved
  1: string manifoldId;
}

struct ExportTableResponse {
  1: string tableName;
  2: string tableNamespace;
  3: string urlToTable;
}

////
// V2 structs
////

@hack.MigrationBlockingAllowInheritance
struct Handle {
  1: string manifoldId;
  2: string host;
  3: i64 port;
  4: i64 taskId;
}

struct HandleRequest {
  1: string manifoldId;
  // Idle TTL in seconds. Non-positive values use the 1-hour default; huge
  // values are clamped. getHandle only raises an existing sandbox's TTL.
  2: i64 ttlSeconds = 3600;
  // Caller label for logs; empty when unset.
  3: string clientId;
}

struct HandleResponse {
  1: Handle handle;
}

@hack.Attributes{
  attributes = [
    "JSEnum(shape('flow_enum' => false))",
    "GraphQLEnum('XFBRebalancerExplorerSandboxStatus')",
    "GraphQLUnprefixedNamingScheme",
    "SelfDescriptive",
    "Oncalls('algopt')",
  ],
}
enum SandboxStatus {
  NOT_LOADED = 1,
  LOADING = 2,
  LOADED = 3,
}

struct SandboxStatusResponse {
  1: SandboxStatus status;
}

struct ProblemMetadataResponse {
  1: ProblemMetadata metadata;
}

struct DataRequest {
  1: Query query;
}

struct DataResponse {
  1: Result result;
}

struct EvaluateRequest {
  1: Assignment assignment;
}

struct EvaluateResponse {
  1: EvaluationResult result;
}

struct GoalSpecRequest {
  1: string name;
}

struct GoalSpecResponse {
  1: GoalSpec spec;
}

struct ConstraintSpecRequest {
  1: string name;
}

struct ConstraintSpecResponse {
  1: ConstraintSpec spec;
}

struct LocalSearchProfilesResponse {
  1: list<Types.LocalSearchProfile> profiles;
}

struct ExportTableRequest {
  1: string tableName;
  2: optional Assignment assignmentA;
  3: optional Assignment assignmentB;
}

////
// Server Status
////

struct ServerStatus {
  1: i64 loadingSandboxCount;
  2: i64 loadedSandboxCount;
  3: i64 freeMemoryBytes;
  4: i64 usedMemoryBytes;
}

////
// Service
////

service RebalancerExplorerService {
  ////
  // V2
  ////
  HandleResponse getHandle(1: HandleRequest request);

  SandboxStatusResponse getSandboxStatus(1: Handle handle);

  // Server-level status: sandbox counts and memory usage. Used for server
  // selection when deciding where to load a new sandbox.
  ServerStatus getServerStatus();

  ProblemMetadataResponse getProblemMetadataV2(1: Handle handle);

  DataResponse getDataV2(1: Handle handle, 2: DataRequest request);

  EvaluateResponse evaluateV2(1: Handle handle, 2: EvaluateRequest request);

  Result evaluateMetricCollection(
    1: Handle handle,
    2: DataRequest request,
    3: EvaluateRequest evaluateRequestA,
    4: EvaluateRequest evaluateRequestB,
  );

  GoalSpecResponse getGoalSpecV2(1: Handle handle, 2: GoalSpecRequest request);
  ConstraintSpecResponse getConstraintSpecV2(
    1: Handle handle,
    2: ConstraintSpecRequest request,
  );

  TypeaheadResponse getTypeaheadV2(
    1: Handle handle,
    2: TypeaheadRequest request,
  );

  MovesBetweenAssignmentsResponse getMovesBetweenAssignmentsV2(
    1: Handle handle,
    2: MovesBetweenAssignmentsRequest request,
  );

  TreeNodeResponse getTreeNodeV2(1: Handle handle, 2: TreeNodeRequest request);

  MetricDistributionResponse getMetricDistributionV2(
    1: Handle handle,
    2: MetricDistributionRequest request,
  );

  LocalSearchProfilesResponse getLocalSearchProfilesV2(1: Handle handle);

  MoveSetsResponse getMoveSets(1: Handle handle, 2: MoveSetsRequest request);

  EditProblemResponse editProblemV2(
    1: Handle handle,
    2: EditProblemRequest request,
  );

  ExportTableResponse exportTable(
    1: Handle handle,
    2: ExportTableRequest request,
  );
}
