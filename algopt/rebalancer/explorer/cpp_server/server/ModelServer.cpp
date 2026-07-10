#include "rebalancer/explorer/cpp_server/server/ModelServer.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/interface/serialization/Serializer.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "rebalancer/explorer/cpp_server/lib/FilterModel.h"
#include "rebalancer/explorer/cpp_server/lib/GroupModel.h"
#include "rebalancer/explorer/cpp_server/lib/LoadModel.h"
#include "rebalancer/explorer/cpp_server/lib/MetricsTabulator.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/Future.h>
#include <folly/system/HardwareConcurrency.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/common/UuidGenerator.h"
#include "algopt/rebalancer/interface/fb/Manifold.h"
#include "rebalancer/explorer/cpp_server/lib/fb/PrestoUtils.h"
#endif

#include <algorithm>
#include <iterator>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Constants for exportTable
const static std::string kPrestoNamespace = "infrastructure";
constexpr static int kRetentionDays = 3;
constexpr static std::string_view kDeltaSymbol = "\u0394";
constexpr static double kMaxSecsToComputeChangeInObjectives = 60.0;
const static std::string kObjectCountColDesc =
    "Value in row-i is the number of objects of the corresponding group in row-i that moved from source to destination";
const static std::string kStageIdColDesc =
    "Value w.r.t. a moveSet is the local search stage during which the moveSet was applied";
const static std::string kCycleIdColDesc =
    "Value w.r.t. a moveSet is the local search cycle during which the moveSet was applied";
const static std::string kObjectiveColDesc =
    "Value w.r.t. moveSet i is the change in value of this objective as a result of applying moveSet i";
const static std::string kMoveSetColDesc =
    "MoveSet i is the set of moves applied together during the i-th step of local search.";
} // namespace

namespace facebook::rebalancer::explorer {

using namespace facebook::rebalancer::entities;

ModelServer::ModelServer(interface::Bundle&& bundle) {
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  problemSpec_ = std::move(explorerModel.problemSpec);
  universe_ = std::move(explorerModel.universe);
  solution_ = std::move(explorerModel.solution);
  materialized_ = std::move(explorerModel.materialized);
  initialAssignment_ =
      rebalancer::Assignment(materialized_->updatedInitialAssignment);
  finalAssignment_ = std::move(explorerModel.finalAssignment);
  dynamicDimensionNames_ = std::move(explorerModel.dynamicDimensionNames);
  solverSpec_ = problemSpec_.strategy()->solvers()->at(0);
  if (materialized_->metrics) {
    // Build the metricCollectionNameToType_ map synchronously (cheap — just
    // iterates available collections). The expensive fullApply is deferred
    // to async below.
    for (auto& [type, collection] :
         materialized_->metrics->getAvailableCollections()) {
      auto enumName = apache::thrift::util::enumNameSafe(type);
      std::transform(
          enumName.begin(), enumName.end(), enumName.begin(), [](char c) {
            return (c == '_') ? ' ' : std::tolower(c);
          });
      metricCollectionNameToType_.emplace(std::move(enumName), type);
    }
  }
  problem_ = std::move(explorerModel.problem);
  equivalenceSetsData_ = std::move(explorerModel.equivalenceSetsData);

  for (auto partitionId : universe_->getPartitionIds()) {
    partitionNames_.insert(universe_->getEntityName(partitionId));
  }

  // Add equivalence set partition name to partitionNames_; useful to treat
  // equivalence sets as a partition for groupBy kind of operations
  partitionNames_.insert(equivalenceSetsData_.partitionName);

  executor_ = std::make_shared<folly::CPUThreadPoolExecutor>(
      folly::available_concurrency(),
      std::make_unique<folly::LifoSemMPMCQueue<
          folly::CPUThreadPoolExecutor::CPUTask,
          folly::QueueBehaviorIfFull::BLOCK>>(
          folly::CPUThreadPoolExecutor::kDefaultMaxQueueSize),
      std::make_shared<folly::NamedThreadFactory>("CPUThreadPoolExecutor"));

  startTableDataAsync(std::move(explorerModel.tableData));

  initExpressionIdToPtr();

  // Start initPropertiesIndex asynchronously - will be awaited in getTreeNode
  startPropertiesIndexAsync();

  startMetricsAndObjectiveInitAsync();
}

ModelServer::~ModelServer() {
  try {
    waitForPropertiesIndex();
  } catch (const std::exception& e) {
    XLOG(ERR) << "Exception in waitForPropertiesIndex: " << e.what();
  }
  try {
    waitForMetricsAndObjectiveInit();
  } catch (const std::exception& e) {
    XLOG(ERR) << "Exception in waitForMetricsAndObjectiveInit: " << e.what();
  }
  try {
    waitForTableData();
  } catch (const std::exception& e) {
    XLOG(ERR) << "Exception in waitForTableData: " << e.what();
  }
  try {
    folly::coro::blockingWait(asyncScope_.joinAsync());
  } catch (const std::exception& e) {
    XLOG(ERR) << "Exception in asyncScope join: " << e.what();
  }
}

void ModelServer::startTableDataAsync(
    entities::Map<std::string, Table> tableData) {
  tableDataFuture_ =
      co_withExecutor(
          executor_.get(),
          folly::coro::co_invoke(
              [this, tableData = std::move(tableData)]() mutable
                  -> folly::coro::Task<folly::Unit> {
                LoadModel::buildStaticObjectDimensionCols(
                    *universe_, tableData);

                if (!dynamicDimensionNames_.empty()) {
                  LoadModel::initDynamicDimensionTables(
                      *universe_,
                      finalAssignment_,
                      tablePromises_,
                      asyncScope_,
                      executor_.get());
                }

                auto objectsTableName = universe_->getObjectTypeName();

                for (auto it = tableData.begin(); it != tableData.end();) {
                  if (it->first == objectsTableName) {
                    ++it;
                    continue;
                  }
                  auto promise =
                      std::make_shared<folly::SharedPromise<Table>>();
                  promise->setValue(std::move(it->second));
                  tablePromises_[it->first] = std::move(promise);
                  it = tableData.erase(it);
                }

                auto promise = std::make_shared<folly::SharedPromise<Table>>();
                tablePromises_[objectsTableName] = promise;

                asyncScope_.add(
                    folly::coro::co_withExecutor(
                        executor_.get(),
                        folly::coro::co_invoke(
                            [this,
                             tableData = std::move(tableData),
                             promise = std::move(promise)]() mutable
                                -> folly::coro::Task<void> {
                              try {
                                if (!dynamicDimensionNames_.empty()) {
                                  co_await LoadModel::
                                      initDynamicObjectDimensionColsAsync(
                                          *universe_,
                                          finalAssignment_,
                                          tableData,
                                          asyncScope_,
                                          executor_);
                                }

                                auto& objectsTable = tableData.at(
                                    universe_->getObjectTypeName());

                                objectsTable.insertColumnsInSortedOrder(
                                    LoadModel::buildPartitionCols(
                                        *universe_, equivalenceSetsData_));
                                for (auto& col : LoadModel::buildAssignmentCols(
                                         *universe_, finalAssignment_)) {
                                  objectsTable.insertColumn(std::move(col));
                                }

                                promise->setValue(std::move(objectsTable));
                              } catch (...) {
                                promise->setException(
                                    folly::exception_wrapper(
                                        std::current_exception()));
                              }
                            })));

                co_return folly::unit;
              }))
          .start();
}

void ModelServer::waitForTableData() const {
  folly::call_once(tableDataOnceFlag_, [this]() {
    if (tableDataFuture_.valid()) {
      std::move(tableDataFuture_).get();
    }
  });
}

void ModelServer::initObjectiveNameToExpr() {
  for (const auto& [constraintId, expr] : materialized_->softConstraints) {
    if (!expr) {
      continue;
    }
    objectiveNameToExpr_.emplace(universe_->getEntityName(constraintId), expr);
  }
  for (const auto& [goalId, expr] : materialized_->userGoals) {
    if (!expr) {
      continue;
    }
    objectiveNameToExpr_.emplace(universe_->getEntityName(goalId), expr);
  }

  const auto& moveStatsSpec = *problemSpec_.moveStatsSpec();
  const auto moveStatsEnabled =
      *moveStatsSpec.trackContainers() || *moveStatsSpec.trackObjects();
  const auto allObjChangesRecorded =
      *moveStatsSpec.showAllChangedObjectivesInMovesSummary();
  if (moveStatsEnabled && allObjChangesRecorded) {
    allObjChangesInMovesSummary_ = true;
    canDisplayObjChangesInMoveSetsTable_ = true;
  } else if (auto changes = computeObjectiveToChangePerMoveSet()) {
    objectiveToChangePerMoveSet_.emplace(std::move(*changes));
    canDisplayObjChangesInMoveSetsTable_ = true;
  }

  // check if problem only has single moves (i.e., all moveSets are of size 1)
  if (solution_ && solution_->movesSummary()) {
    const auto& moveSets = *solution_->movesSummary();
    for (const auto& moveSet : moveSets) {
      if (moveSet.moves()->size() > 1) {
        problemOnlyHasSingleMoves_ = false;
        break;
      }
    }
  }
}

std::optional<folly::F14FastMap<std::string, std::vector<double>>>
ModelServer::computeObjectiveToChangePerMoveSet() const {
  if (!solution_ || !solution_->movesSummary()) {
    return std::nullopt;
  }
  // If per move-set objective changes were not pre-computed
  // we compute them by "simulating" applying of moves in the same
  // order and recording the improvements.
  const algopt::Timer timer(true);
  const auto& moveSets = *solution_->movesSummary();

  // 1. make an orchestrator with just the objective exprs
  std::vector<Expression*> objExprs;
  objExprs.reserve(objectiveNameToExpr_.size());
  std::transform(
      objectiveNameToExpr_.begin(),
      objectiveNameToExpr_.end(),
      std::back_inserter(objExprs),
      [](const auto& nameExprPair) { return nameExprPair.second.get(); });

  const auto objectCount = universe_->getNumObjects();
  const auto containerCount =
      universe_->getContainers().getContainerIds().size();
  Orchestrator orchestrator;
  orchestrator.init(
      std::move(objExprs),
      AffectedByChangeDecisionData(
          static_cast<int>(objectCount), static_cast<int>(containerCount)));

  // 2. apply all the moves and collect result
  folly::F14FastMap<std::string, std::vector<double>>
      objectiveToChangePerMoveSet;
  auto assignment = folly::copy(initialAssignment_);
  for (const auto& moveSet : moveSets) {
    // if there are a lot of moves, it takes too much time to apply them and
    // record the change in objectives; in such cases, skip. Explorer will show
    // a warning saying seeing change in objectives is not supported for the
    // problem
    // TODO: add a way to clone the expression graph so that we can set up this
    // function as an async process and let it keep running in the background
    if (timer.getSeconds() > kMaxSecsToComputeChangeInObjectives) {
      XLOGF(
          INFO,
          "Could not finish computing change in objectives w.r.t. moveSets within {}s; skipping",
          kMaxSecsToComputeChangeInObjectives);
      return std::nullopt;
    }

    ChangeSet changeSet;
    for (const auto& move : *moveSet.moves()) {
      const auto objId = universe_->getObjectId(*move.object());
      const auto srcContainerId =
          universe_->getContainerId(*move.srcContainer());
      const auto dstContainerId =
          universe_->getContainerId(*move.dstContainer());
      changeSet.insert(Change(objId, srcContainerId, -1));
      changeSet.insert(Change(objId, dstContainerId, +1));

      assignment.moveTo(objId, dstContainerId);
    }

    Context context;
    context.changes() = std::move(changeSet);
    orchestrator.apply(context, assignment);
    for (const auto& [objective, expr] : objectiveNameToExpr_) {
      objectiveToChangePerMoveSet[objective].emplace_back(expr->value);
    }
  }

  // 3. make sure to apply all the exprs with the initial assignment
  ChangeSet reverseChangeSet;
  for (const auto& [object, initialContainer] :
       initialAssignment_.getObjectToContainerMap()) {
    const auto finalContainer = assignment.getContainer(object);
    if (finalContainer != initialContainer) {
      reverseChangeSet.insert(Change(object, finalContainer, -1));
      reverseChangeSet.insert(Change(object, initialContainer, +1));

      assignment.moveTo(object, initialContainer);
    }
  }

  Context reverseContext;
  reverseContext.changes() = std::move(reverseChangeSet);
  orchestrator.apply(reverseContext, assignment);

  XLOGF(
      INFO,
      "time to compute change in objectives w.r.t. {} moves: {}",
      moveSets.size(),
      timer.getSeconds());
  return objectiveToChangePerMoveSet;
}

rebalancer::Assignment ModelServer::getInitialAssignment() const {
  return initialAssignment_;
}

double ModelServer::getChangeInObjective(
    const std::string& objectiveName,
    const size_t moveSetIndex) const {
  waitForMetricsAndObjectiveInit();
  if (!solution_ || !solution_->movesSummary()) {
    throw std::runtime_error(
        "Unexpected call to getChangeInObjective() when solution or movesSummary is unset");
  }
  if (allObjChangesInMovesSummary_) {
    const auto& moveSet = solution_->movesSummary()->at(moveSetIndex);
    const auto valuePtr = folly::get_ptr(*moveSet.objectives(), objectiveName);
    // change() recorded in moveSummary is (oldValue - newValue) and hence we
    // use -1 below
    return valuePtr ? -1 * valuePtr->change().value() : 0.0;
  } else if (objectiveToChangePerMoveSet_) {
    const auto& changePerMoveSet =
        objectiveToChangePerMoveSet_->at(objectiveName);
    const auto newValue = changePerMoveSet.at(moveSetIndex);
    const auto oldValue = (moveSetIndex >= 1)
        ? changePerMoveSet.at(moveSetIndex - 1)
        : objectiveNameToExpr_.at(objectiveName)->value; // initial value
    return newValue - oldValue;
  } else {
    throw std::runtime_error(
        "Unexpected call to getObjectiveChange when allObjChangesInMovesSummary_ = false and objectiveToChangePerMoveSet_ is empty");
  }
}

static Result prepareResult(const Table& table, size_t totalRows) {
  /* Prepare Result object from filtered entity ids and table data. */
  const auto& columns = table.getColumnData();
  std::vector<ColumnDescription> columnDescriptions;
  std::transform(
      columns.begin(),
      columns.end(),
      std::back_inserter(columnDescriptions),
      [](auto column) {
        ColumnDescription description;
        description.name() = column->getColumnName();
        description.type() = column->getColumnType();
        description.primaryKey() = column->isPrimaryKey();
        description.description() = column->getDescription();
        return description;
      });

  std::vector<RowData> rows;
  for (auto entityId : table.getRowIds()) {
    std::vector<CellData> cells;
    for (const auto& column : columns) {
      CellData cellData;
      const auto& dataCell = column->getValue(entityId);
      if (dataCell.strValue != std::nullopt) {
        cellData.stringValue() = *dataCell.strValue;
      } else if (dataCell.doubleValue != std::nullopt) {
        cellData.doubleValue() = *dataCell.doubleValue;
      } else {
        throw std::runtime_error("Column data not found");
      }
      cells.push_back(std::move(cellData));
    }

    RowData rowData;
    rowData.cells() = std::move(cells);
    rows.push_back(std::move(rowData));
  }
  Result result;
  result.columns() = std::move(columnDescriptions);
  result.totalCount() = totalRows;
  result.rows() = std::move(rows);
  return result;
}

static Table applyOrder(const Order& order, Table table) {
  /* Sorts the table based on requested column */

  auto rowIds = table.getRowIds();
  const auto& orderColumns = order.columns();
  if (orderColumns->size() > 1) {
    throw std::runtime_error("Ordering for multiple columns is not supported");
  }
  const auto& orderRequest = orderColumns->at(0);
  const auto& tableColumns = table.getColumnData();
  const auto& orderTableColumn =
      Utils::fetchColumn(tableColumns, *orderRequest.name());
  const auto& orderDirection = *orderRequest.direction();

  std::sort(
      rowIds.begin(),
      rowIds.end(),
      [orderDirection, &orderTableColumn](EntityId id1, EntityId id2) {
        auto& value1 = orderTableColumn->getValue(id1);
        auto& value2 = orderTableColumn->getValue(id2);
        if (value1.doubleValue != std::nullopt) {
          if (orderDirection == OrderDirection::ASCENDING) {
            return value1.doubleValue < value2.doubleValue;
          } else {
            return value1.doubleValue > value2.doubleValue;
          }
        } else {
          if (orderDirection == OrderDirection::ASCENDING) {
            return value1.strValue < value2.strValue;
          } else {
            return value1.strValue > value2.strValue;
          }
        }
      });
  table.updateRowIds(rowIds);
  return table;
}

static Table applyPagination(const Page& page, Table table) {
  /* Apply pagination. */
  const auto& rowIds = table.getRowIds();
  const int startIndex = std::min(*page.offset(), int(rowIds.size()));
  const int limit = *page.limit();
  const int endIndex = std::min(startIndex + limit, int(rowIds.size()));
  std::vector<EntityId> newRowIds(
      rowIds.begin() + startIndex, rowIds.begin() + endIndex);
  table.updateRowIds(std::move(newRowIds));
  return table;
}

Result ModelServer::processQueryOnTable(
    const Query& query,
    explorer::Table table) {
  if (query.filter()) {
    table = FilterModel::applyFilter(*query.filter(), std::move(table));
  }

  if (query.group()) {
    table = GroupModel::applyGroup(*query.group(), std::move(table));
  }

  if (query.order()) {
    table = applyOrder(*query.order(), std::move(table));
  }

  const auto totalRows = table.getRowIds().size();
  if (query.page()) {
    table = applyPagination(*query.page(), std::move(table));
  }

  return prepareResult(table, totalRows);
}

folly::coro::Task<Result> ModelServer::getData(const Query& query) const {
  waitForTableData();
  const auto& entity = *query.entity();
  const auto promisePtr = folly::get_ptr(tablePromises_, entity);
  if (!promisePtr) {
    throw std::runtime_error(fmt::format("Unknown entity: '{}'", entity));
  }
  const auto tableData = co_await (*promisePtr)->getSemiFuture();
  co_return processQueryOnTable(query, tableData);
}

static std::string convertToLower(std::string request) {
  std::for_each(
      request.begin(), request.end(), [](char& c) { c = ::tolower(c); });
  return request;
}

folly::coro::Task<TypeaheadResponse> ModelServer::getTypeahead(
    const TypeaheadRequest& request) const {
  waitForTableData();
  const auto& query = convertToLower(*request.query());
  std::vector<std::string> matchingRows;
  const auto addToMatchingRows = [&matchingRows, &query](const auto& rowName) {
    if (convertToLower(rowName).rfind(query, 0) == 0) {
      matchingRows.push_back(rowName);
    }
  };

  const auto& entity = *request.entity();
  if (auto promisePtr = folly::get_ptr(tablePromises_, entity)) {
    const auto table = co_await (*promisePtr)->getSemiFuture();
    const auto pk = table.getOnlyPrimaryKeyColumn();
    for (auto rowId : table.getRowIds()) {
      addToMatchingRows(pk->getValue(rowId).toString());
    }
  } else if (partitionNames_.contains(entity)) {
    // check if it is equivalence set partition
    if (entity == equivalenceSetsData_.partitionName) {
      for (const auto& groupName : equivalenceSetsData_.groupNames) {
        addToMatchingRows(groupName);
      }
    } else {
      auto partitionId = universe_->getPartitionId(entity);
      for (auto groupId : universe_->getPartition(partitionId).getGroupIds()) {
        addToMatchingRows(universe_->getEntityName(groupId));
      }
    }
  } else {
    throw std::runtime_error(
        fmt::format("Unknown entity {} for getTypeahead", entity));
  }

  std::sort(matchingRows.begin(), matchingRows.end());

  auto limit = std::min(*request.limit(), int(matchingRows.size()));
  TypeaheadResponse response;
  response.matches() = std::vector<std::string>(
      std::make_move_iterator(matchingRows.begin()),
      std::make_move_iterator(matchingRows.begin() + limit));
  co_return response;
}

ProblemMetadata ModelServer::getProblemMetadata() const {
  waitForMetricsAndObjectiveInit();
  std::vector<std::string> scopeNames;
  for (auto scopeId : universe_->getScopeIds()) {
    const auto& scopeName = universe_->getEntityName(scopeId);
    if (scopeName == universe_->getContainerTypeName()) {
      continue;
    }
    scopeNames.push_back(universe_->getEntityName(scopeId));
  }

  std::sort(scopeNames.begin(), scopeNames.end());

  auto& objectName = universe_->getObjectTypeName();
  ProblemMetadata metadata;
  metadata.objectName() = objectName;
  metadata.containerName() = universe_->getContainerTypeName();
  metadata.scopeNames() = std::move(scopeNames);
  metadata.dynamicDimensionNames() = dynamicDimensionNames_;
  metadata.variableName() = objectName;
  metadata.hasFinalAssignment() = !finalAssignment_.empty();
  metadata.runId() = *problemSpec_.runId();
  metadata.partitionNames().value().assign(
      partitionNames_.begin(), partitionNames_.end());
  if (auto movesSummary = solution_->movesSummary();
      movesSummary && movesSummary->size() > 0) {
    metadata.hasIntermediateAssignment() = true;
    metadata.numSteps() = movesSummary->size();
  } else {
    metadata.hasIntermediateAssignment() = false;
  }
  metadata.metricCollectionNames() = metricCollectionNameToType_ |
      std::ranges::views::keys | algopt::utils::to<std::vector>;
  metadata.objectiveNames() = objectiveNameToExpr_ | std::ranges::views::keys |
      algopt::utils::to<std::vector>;
  metadata.canDisplayObjChangesInMoveSetsTable() =
      canDisplayObjChangesInMoveSetsTable_;
  metadata.hasOnlySingleMoves() = problemOnlyHasSingleMoves_;

  metadata.serviceName() = *problemSpec_.service_();

  metadata.serviceScope() = *problemSpec_.scope();

  // Derive solver type string from solver spec
  switch (solverSpec_.getType()) {
    case SolverT::Type::optimalSolverSpec:
      metadata.solverType() = "Optimal";
      break;
    case SolverT::Type::optimalSubsetSolverSpec:
      metadata.solverType() = "OptimalSubsetSolver";
      break;
    case SolverT::Type::localSearchStageSolverSpec:
      metadata.solverType() = "LocalSearchStageSolver";
      break;
    case SolverT::Type::localSearchSolverSpec:
      metadata.solverType() = "LocalSearch";
      break;
    case SolverT::Type::rasHybridSolverSpec:
      metadata.solverType() = "RasHybrid";
      break;
    default:
      throw std::runtime_error(
          fmt::format(
              "Unknown solver type: {}",
              static_cast<int>(solverSpec_.getType())));
  }

  // Extract solver end reason from solution
  if (solution_ && !solution_->solverSummaries()->empty()) {
    const auto& lastSummary = solution_->solverSummaries()->back();
    metadata.solverEndReason() =
        apache::thrift::util::enumNameSafe(*lastSummary.endReason());
  }

  // Extract total runtime from solution
  if (solution_) {
    metadata.totalRuntime() = *solution_->problemProfile()->solveSec();
  }

  // Entity counts
  metadata.numObjects() = universe_->getNumObjects();
  metadata.numContainers() =
      std::ranges::distance(universe_->getContainers().getContainerIds());
  metadata.numDimensions() = universe_->getObjects().getDimensionCount();
  metadata.numScopes() = std::ranges::distance(universe_->getScopeIds());

  // Entity name fingerprints for cross-run compatibility checks.
  // Two runs with identical object/container names produce the same
  // fingerprint.
  const auto namesFingerprint = [&](const auto& ids) {
    auto names =
        ids | std::views::transform([&](auto id) -> const std::string& {
          return universe_->getEntityName(id);
        });
    return fmt::format(
        "{:016x}",
        folly::hash::commutative_hash_combine_range(
            names.begin(), names.end()));
  };
  metadata.objectNamesFingerprint() =
      namesFingerprint(universe_->getObjects().getObjectIds());
  metadata.containerNamesFingerprint() =
      namesFingerprint(universe_->getContainers().getContainerIds());

  return metadata;
}

entities::Map<entities::ObjectId, entities::ContainerId>
ModelServer::getObjectToContainer(const Assignment& assignment) const {
  entities::Map<entities::ObjectId, entities::ContainerId> objectToContainer;

  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      baseAssignment;
  switch (*assignment.base()) {
    case AssignmentBase::INITIAL:
      baseAssignment = materialized_->updatedInitialAssignment;
      break;
    case AssignmentBase::FINAL:
      baseAssignment = finalAssignment_;
      break;
    case AssignmentBase::INTERMEDIATE:
      baseAssignment = buildIntermediateAssignment(assignment);
      break;
  }

  for (auto& [baseContainerId, objectIds] : baseAssignment) {
    for (auto objectId : objectIds) {
      auto& objectName = universe_->getEntityName(objectId);
      auto overrideContainerName =
          folly::get_ptr(*assignment.variableToContainerOverride(), objectName);
      auto desiredContainerId = overrideContainerName == nullptr
          ? baseContainerId
          : universe_->getContainerId(*overrideContainerName);
      objectToContainer.emplace(objectId, desiredContainerId);
    }
  }

  return objectToContainer;
}

entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
ModelServer::buildIntermediateAssignment(const Assignment& assignment) const {
  if (!assignment.searchStep()) {
    throw std::runtime_error("Intermediate assignment needs a search step");
  }
  const int64_t searchStep = *assignment.searchStep();
  if (!solution_) {
    throw std::runtime_error("No AssignmentSolution");
  }
  if (!(solution_->movesSummary())) {
    throw std::runtime_error("No movesSummary in AssignmentSolution");
  }
  auto& movesSummary = *solution_->movesSummary();
  if (searchStep > static_cast<int64_t>(movesSummary.size())) {
    throw std::runtime_error(
        fmt::format(
            "searchStep must be less or equal than the number of moves ({})",
            movesSummary.size()));
  }

  auto intermediateAssignment = materialized_->updatedInitialAssignment;
  for (int64_t i = 0; i < searchStep; i++) {
    auto step = movesSummary.at(i);
    for (const auto& move : *step.moves()) {
      auto object = universe_->getObjectId(*move.object());
      auto src = universe_->getContainerId(*move.srcContainer());
      auto dst = universe_->getContainerId(*move.dstContainer());
      auto& srcObjects = intermediateAssignment.at(src);
      for (auto it = srcObjects.begin(); it != srcObjects.end(); ++it) {
        if (*it == object) {
          srcObjects.erase(it);
          break;
        }
      }
      intermediateAssignment.at(dst).push_back(object);
    }
  }
  return intermediateAssignment;
}

ChangeSet ModelServer::getChangesFromInitial(
    const Assignment& assignment) const {
  auto objectToContainer = getObjectToContainer(assignment);

  MoveSet moves;
  for (auto& [initialContainerId, objectIds] :
       materialized_->updatedInitialAssignment) {
    for (auto objectId : objectIds) {
      auto desiredContainerId = objectToContainer.at(objectId);
      if (initialContainerId != desiredContainerId) {
        moves.insert(Move(objectId, initialContainerId, desiredContainerId));
      }
    }
  }

  return moves.getChangeSet();
}

MovesBetweenAssignmentsResponse ModelServer::getMovesBetweenAssignments(
    const MovesBetweenAssignmentsRequest& request) const {
  auto source = getObjectToContainer(*request.source());
  auto destination = getObjectToContainer(*request.destination());

  std::map<std::string, std::string> difference;
  for (auto& [objectId, dstContainerId] : destination) {
    auto srcContainerId = source.at(objectId);
    if (srcContainerId != dstContainerId) {
      difference.emplace(
          universe_->getEntityName(objectId),
          universe_->getEntityName(dstContainerId));
    }
  }

  MovesBetweenAssignmentsResponse response;
  response.variableToContainer() = std::move(difference);
  return response;
}

MoveSetsResponse ModelServer::getMoveSets(
    const MoveSetsRequest& request) const {
  waitForMetricsAndObjectiveInit();
  if (!solution_.has_value()) {
    throw std::runtime_error("No AssignmentSolution found");
  }
  if (!solution_->movesSummary().has_value()) {
    throw std::runtime_error("No movesSummary in AssignmentSolution");
  }

  const auto& movesSummary = *solution_->movesSummary();
  const auto& assignmentA = *request.assignmentA();
  const auto& assignmentB = *request.assignmentB();
  if (!assignmentA.variableToContainerOverride()->empty() ||
      !assignmentB.variableToContainerOverride()->empty()) {
    throw std::runtime_error(
        "Expected empty variableToContainerOverride in assignments");
  }

  const auto getMoveSetIndex =
      [&movesSummary](const Assignment& assignment) -> int64_t {
    switch (assignment.base().value()) {
      case AssignmentBase::INITIAL:
        return 0;
      case AssignmentBase::FINAL:
        return static_cast<int64_t>(movesSummary.size());
      case AssignmentBase::INTERMEDIATE:
        if (!assignment.searchStep().has_value()) {
          throw std::runtime_error(
              "Expected searchStep in assignment with base INTERMEDIATE");
        }
        return *assignment.searchStep();
    }
    throw std::runtime_error("Unexpected assignment base");
  };

  const auto startMoveSetIdx = getMoveSetIndex(assignmentA);
  const auto endMoveSetIdx = getMoveSetIndex(assignmentB);
  if (startMoveSetIdx < 0 || endMoveSetIdx < 0) {
    throw std::runtime_error(
        "startMoveSetIdx or endMoveSetIdx cannot be negative");
  }
  if (startMoveSetIdx > endMoveSetIdx) {
    throw std::runtime_error("startMoveSetIdx should be at most endMoveSetIdx");
  }

  if (endMoveSetIdx > static_cast<int64_t>(movesSummary.size())) {
    throw std::runtime_error(
        fmt::format(
            "endMoveSetIdx must be at most the number of moves ({})",
            movesSummary.size()));
  }
  auto objectiveNamesRef = request.objectiveNames();
  if (!canDisplayObjChangesInMoveSetsTable_ && objectiveNamesRef) {
    throw std::runtime_error(
        "Unexpected request to display change in Objectives when canDisplayObjChangesInMoveSetsTable is false");
  }

  const auto existsPartitionName = request.partitionName().has_value();
  const auto existsScopeName = request.scopeName().has_value();
  const bool hasStageId =
      solverSpec_.getType() == SolverT::Type::localSearchStageSolverSpec ||
      solverSpec_.getType() == SolverT::Type::rasHybridSolverSpec;
  const bool hasCycleId =
      solverSpec_.getType() != SolverT::Type::optimalSolverSpec &&
      solverSpec_.getType() != SolverT::Type::optimalSubsetSolverSpec &&
      std::any_of(movesSummary.begin(), movesSummary.end(), [](const auto& ms) {
        return ms.cycleId().has_value();
      });
  TableBuilder tableBuilder;
  tableBuilder.addColumnDefinition(
      {.name = problemOnlyHasSingleMoves_ ? "Move #" : "MoveSet #",
       .type = ColumnType::IDENTIFIER,
       .isPrimaryKey = true,
       .description = kMoveSetColDesc});
  if (objectiveNamesRef) {
    // these are deliberately placed right next to MoveSet column so that we
    // will only see one value per moveSet in explorer (this because of how
    // XDSPivotTable renders tables)
    for (const auto& objective : *objectiveNamesRef) {
      tableBuilder.addColumnDefinition(
          {.name = fmt::format("{} ({})", objective, kDeltaSymbol),
           .type = ColumnType::DOUBLE,
           .description = kObjectiveColDesc});
    }
  }
  tableBuilder
      .addColumnDefinition(
          {.name = existsPartitionName ? "Group" : "Object",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = existsScopeName ? "Source ScopeItem" : "Source Container",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = existsScopeName ? "Destination ScopeItem"
                                   : "Destination Container",
           .type = ColumnType::STRING,
           .isPrimaryKey = true});
  if (existsPartitionName) {
    tableBuilder.addColumnDefinition(
        {.name = "Object Count",
         .type = ColumnType::INTEGER,
         .isPrimaryKey = true,
         .description = kObjectCountColDesc});
  }
  if (hasStageId) {
    tableBuilder.addColumnDefinition(
        {.name = "Stage Id",
         .type = ColumnType::INTEGER,
         .isPrimaryKey = true,
         .description = kStageIdColDesc,
         .excludeFromAggregation = true});
  }
  if (hasCycleId) {
    tableBuilder.addColumnDefinition(
        {.name = "Cycle Id",
         .type = ColumnType::INTEGER,
         .description = kCycleIdColDesc,
         .excludeFromAggregation = true});
  }

  const auto scopePtr = request.scopeName().has_value()
      ? &universe_->getScope(universe_->getScopeId(*request.scopeName()))
      : nullptr;
  for (auto moveSetIdx = startMoveSetIdx; moveSetIdx < endMoveSetIdx;
       ++moveSetIdx) {
    const auto& moveSet = movesSummary.at(moveSetIdx);
    folly::F14FastMap<std::tuple<std::string, std::string, std::string>, int>
        groupSrcDstToCount;
    for (const auto& move : *moveSet.moves()) {
      // use the partition name if it exists, otherwise use the object name
      auto groupName = existsPartitionName
          ? getGroupName(
                *move.object(),
                apache::thrift::can_throw(request.partitionName().value()))
          : folly::copy(*move.object());
      auto src = getScopeItemName(*move.srcContainer(), scopePtr);
      auto dst = getScopeItemName(*move.dstContainer(), scopePtr);
      auto key =
          std::make_tuple(std::move(groupName), std::move(src), std::move(dst));
      groupSrcDstToCount[std::move(key)]++;
    }

    for (const auto& [grpSrcDst, count] : groupSrcDstToCount) {
      const auto& [grpName, srcName, dstName] = grpSrcDst;
      // shift indices by one so that the moveset indices start at 1 instead
      // of
      // 0
      const auto moveSetId = moveSetIdx + 1;
      std::vector<DataCell> row;
      row.emplace_back(moveSetId);

      if (objectiveNamesRef) {
        for (const auto& objName : *objectiveNamesRef) {
          row.emplace_back(getChangeInObjective(objName, moveSetIdx));
        }
      }

      row.emplace_back(grpName);
      row.emplace_back(srcName);
      row.emplace_back(dstName);
      if (existsPartitionName) {
        row.emplace_back(count);
      }
      if (hasStageId) {
        // for really old saved instances, it is possible that stageId is not
        // set, so use NaN to indicate that
        const auto stageId = moveSet.stageId().has_value()
            ? *moveSet.stageId()
            : std::numeric_limits<double>::quiet_NaN();
        row.emplace_back(stageId);
      }
      if (hasCycleId) {
        const auto cycleId = moveSet.cycleId().has_value()
            ? *moveSet.cycleId()
            : std::numeric_limits<double>::quiet_NaN();
        row.emplace_back(cycleId);
      }
      tableBuilder.addRowWithCells(row);
    }
  }

  MoveSetsResponse response;
  response.table() =
      processQueryOnTable(*request.query(), tableBuilder.build());
  return response;
}

std::string ModelServer::getGroupName(
    const std::string& objectName,
    const std::string& partitionName) const {
  const auto objectId = universe_->getObjectId(objectName);
  if (partitionName == equivalenceSetsData_.partitionName) {
    const auto groupNamePtr =
        folly::get_ptr(equivalenceSetsData_.objectIdToGroupName, objectId);
    return groupNamePtr
        ? *groupNamePtr
        : fmt::format("Not in partition (object: {})", objectName);
  }

  const auto& partition =
      universe_->getPartition(universe_->getPartitionId(partitionName));
  if (!partition.isDisjoint()) {
    throw std::runtime_error("Expected partition to be disjoint");
  }

  const auto groupIdsPtr =
      folly::get_ptr(partition.getObjectIdToGroupIds(), objectId);
  return groupIdsPtr ? universe_->getEntityName(groupIdsPtr->front())
                     : fmt::format("Not in partition (object: {})", objectName);
}

std::string ModelServer::getScopeItemName(
    const std::string& containerName,
    const entities::Scope* scopePtr) const {
  if (!scopePtr) {
    return containerName;
  }

  const auto containerId = universe_->getContainerId(containerName);
  const auto scopeItemId = scopePtr->getScopeItemId(containerId);
  return scopeItemId
      ? universe_->getEntityName(*scopeItemId)
      : fmt::format("Out of scope (container: {})", containerName);
}

static std::vector<int>
linspace(int64_t start, int64_t end, int64_t maxPoints) {
  assert(end >= start);
  assert(maxPoints >= 2);
  const double dx = (end - start) / static_cast<double>(maxPoints - 1);
  std::vector<int> indices;
  indices.reserve(maxPoints);
  for (int64_t i = 0; i < maxPoints; i++) {
    indices.push_back(int(round(start + dx * i)));
  }
  return indices;
}

folly::coro::Task<MetricDistributionResponse>
ModelServer::getMetricDistribution(
    const MetricDistributionRequest& request) const {
  waitForTableData();
  const auto& entity = *request.entity();
  const auto promisePtr = folly::get_ptr(tablePromises_, entity);
  if (!promisePtr) {
    throw std::runtime_error(fmt::format("Unknown entity: '{}'", entity));
  }
  const auto& table = co_await (*promisePtr)->getSemiFuture();
  const auto& seriesColumn =
      Utils::fetchColumn(table.getColumnData(), *request.metric());
  std::vector<double> seriesValues;
  for (auto rowId : table.getRowIds()) {
    seriesValues.push_back(*seriesColumn->getValue(rowId).doubleValue);
  }
  std::sort(seriesValues.begin(), seriesValues.end(), std::greater<>());

  const std::vector<int> sampledIndices = linspace(
      0,
      static_cast<int64_t>(seriesValues.size()) - 1,
      std::min(int(seriesValues.size()), *request.maxPoints()));

  std::vector<MetricDistributionPoint> points;
  for (auto index : sampledIndices) {
    MetricDistributionPoint point;
    point.index() = index;
    point.metricValue() = seriesValues.at(index);
    points.push_back(std::move(point));
  }
  MetricDistributionResponse response;
  response.points() = std::move(points);
  co_return response;
}

std::vector<interface::LocalSearchProfile> ModelServer::getLocalSearchProfiles()
    const {
  if (!solution_) {
    return {};
  }

  return *solution_->problemProfile()->localSearchProfiles();
}

const Table& ModelServer::tablulateMetricCollection(
    interface::thrift::MetricCollectionType metricType,
    const Assignment& assignmentA,
    const Assignment& assignmentB) const {
  // Ensure metrics->fullApply() (and the bundled initObjectiveNameToExpr)
  // have completed before tabulating.
  waitForMetricsAndObjectiveInit();
  return *metricCollectionTypeToFullTableCache_.getSavedOrCompute(
      std::make_tuple(metricType, assignmentA, assignmentB), [&]() {
        const auto changeSetA = getChangesFromInitial(assignmentA);
        const auto changeSetB = getChangesFromInitial(assignmentB);
        const auto& orchestrator = problem_->getOrchestrator();
        const auto& universe = problem_->getUniverse();
        const auto& metrics = *materialized_->metrics;
        return std::make_unique<const Table>(tabulateMetricCollection(
            metrics,
            metricType,
            {.universe = universe,
             .orchestrator = orchestrator,
             .changeSetA = changeSetA,
             .changeSetB = changeSetB}));
      });
}

Result ModelServer::evaluateMetricCollection(
    const Query& query,
    const Assignment& assignmentA,
    const Assignment& assignmentB) const {
  if (!materialized_->metrics) {
    throw std::runtime_error("No metrics found");
  }
  const auto& table = tablulateMetricCollection(
      metricCollectionNameToType_.at(*query.entity()),
      assignmentA,
      assignmentB);
  return processQueryOnTable(query, table);
}

EvaluationResult ModelServer::evaluate(const Assignment& assignment) const {
  // Ensure initObjectiveNameToExpr() has completed before evaluating soft
  // constraints / user goals
  waitForMetricsAndObjectiveInit();
  auto& orchestrator = problem_->getOrchestrator();
  EvaluationResult result;
  Context context;
  context.changes() = getChangesFromInitial(assignment);

  // Hard constraints.
  for (auto constraintId : universe_->getConstraints().getConstraintIds()) {
    auto expression = materialized_->hardConstraints.at(constraintId);
    double value = orchestrator.evaluate(expression.get(), context);

    ExpressionResult entry;
    entry.id() = expression->getId();
    entry.type() = ExpressionType::CONSTRAINT;
    entry.name() = universe_->getEntityName(constraintId);
    entry.description() = expression->description;
    entry.value() = value;
    entry.tupleIndex() = 0;
    result.expressions()->push_back(std::move(entry));
  }

  // Soft constraints.
  for (auto constraintId : universe_->getConstraints().getConstraintIds()) {
    auto expressionPtr =
        folly::get_ptr(materialized_->softConstraints, constraintId);
    if (expressionPtr == nullptr) {
      continue;
    }
    auto expression = *expressionPtr;
    double value = orchestrator.evaluate(expression.get(), context);
    auto& constraint = universe_->getConstraints().getConstraint(constraintId);

    ExpressionResult entry;
    entry.id() = expression->getId();
    entry.type() = ExpressionType::OBJECTIVE;
    entry.name() = universe_->getEntityName(constraintId);
    entry.description() = expression->description;
    entry.value() = value;
    entry.tupleIndex() = constraint.getTupleIndex();
    result.expressions()->push_back(std::move(entry));
  }

  // Goals.
  for (auto goalId : universe_->getGoals().getGoalIds()) {
    auto& goal = universe_->getGoals().getGoal(goalId);
    auto expression = materialized_->userGoals.at(goalId);
    double value = orchestrator.evaluate(expression.get(), context);

    ExpressionResult entry;
    entry.id() = expression->getId();
    entry.type() = ExpressionType::OBJECTIVE;
    entry.name() = universe_->getEntityName(goalId);
    entry.description() = expression->description;
    entry.value() = value;
    entry.tupleIndex() = goal.getTupleIndex();
    result.expressions()->push_back(std::move(entry));
  }

  return result;
}

GoalSpec ModelServer::getGoalSpec(const std::string& name) const {
  auto goalId = universe_->getGoalId(name);
  auto& goal = universe_->getGoals().getGoal(goalId);

  GoalSpec result;
  result.name() = name;
  result.weight() = goal.getWeight();
  result.tupleIndex() = goal.getTupleIndex();
  result.specJson() = interface::Serializer::serialize(goal.getSpec());
  result.id() = goalId.asInt();
  return result;
}

ConstraintSpec ModelServer::getConstraintSpec(const std::string& name) const {
  auto constraintId = universe_->getConstraintId(name);
  auto& constraint = universe_->getConstraints().getConstraint(constraintId);

  ConstraintSpec result;
  result.name() = name;
  result.invalidCost() = constraint.getInvalidCost();
  result.invalidState() = constraint.getInvalidState();
  result.policy() = constraint.getPolicy();
  result.specJson() = interface::Serializer::serialize(constraint.getSpec());
  result.id() = constraintId.asInt();
  return result;
}

EditProblemResponse ModelServer::editProblem(
    const EditProblemRequest& request) const {
#ifdef REBALANCER_OSS_BUILD
  std::ignore = request;
  throw std::runtime_error("Editing problems is not supported in OSS Explorer");
#else
  // Create a set of IDs to delete
  std::set<int> toDeleteIds;
  for (const auto& constraint : *request.toDelete().value().constraints()) {
    toDeleteIds.emplace(universe_->getConstraintId(constraint).asInt());
  }
  for (const auto& goal : *request.toDelete().value().goals()) {
    try {
      toDeleteIds.emplace(universe_->getGoalId(goal).asInt());
    } catch (const std::out_of_range&) {
      // soft constraint
      toDeleteIds.emplace(universe_->getConstraintId(goal).asInt());
    }
  }

  // Create a copy of the problem and edit it
  interface::AssignmentProblem editedProblem = folly::copy(problemSpec_);
  editedProblem.runId() = UuidGenerator::genString();
  auto editedUniverse = universe_->toThrift();
  // Filter out deleted IDs from the ID store
  auto editedIdStore = folly::copy(*editedUniverse.idStore());
  editedIdStore.constraintIds() =
      Utils::filterOut(toDeleteIds, *editedIdStore.constraintIds());
  editedIdStore.goalIds() =
      Utils::filterOut(toDeleteIds, *editedIdStore.goalIds());
  editedUniverse.idStore() = std::move(editedIdStore);
  // Filter out deleted constraints and goals
  editedUniverse.constraints().value().constraints() = Utils::filterOut(
      toDeleteIds, *editedUniverse.constraints().value().constraints());
  editedUniverse.goals().value().goals() =
      Utils::filterOut(toDeleteIds, *editedUniverse.goals().value().goals());
  editedProblem.universe() = std::move(editedUniverse);

  // Upload edited problem to Manifold
  interface::Bundle bundle;
  bundle.problem() = std::move(editedProblem);
  interface::Manifold::upload(bundle, *bundle.problem()->runId());

  // Return the response
  EditProblemResponse response;
  response.manifoldId() = std::move(*bundle.problem()->runId());
  return response;
#endif
}

Set<int64_t> ModelServer::getReachableAncestors(
    const std::vector<Expression*>& startNodes) const {
  Set<int64_t> result;
  std::queue<Expression*> toVisit;

  // Initialize queue with all start nodes
  for (auto* node : startNodes) {
    toVisit.push(node);
  }

  const auto& nodeToParents = problem_->getOrchestrator().getNodeToParents();
  while (!toVisit.empty()) {
    auto* current = toVisit.front();
    toVisit.pop();

    if (!result.insert(current->getId()).second) {
      continue;
    }

    if (auto* parentsPtr = folly::get_ptr(nodeToParents, current)) {
      for (const auto& parent : *parentsPtr) {
        toVisit.push(parent);
      }
    }
  }

  return result;
}

TreeNodeResponse ModelServer::getTreeNode(
    const TreeNodeRequest& request) const {
  // Ensure initObjectiveNameToExpr() has completed before evaluating
  // expressions
  waitForMetricsAndObjectiveInit();
  Set<int64_t> relatedNodes;
  if (request.search()->query().has_value()) {
    const auto& searchQuery = *request.search()->query();

    // Wait for entityIdToNodes_ initialization (started async in constructor)
    waitForPropertiesIndex();

    // Collect all matching leaf nodes first
    std::vector<Expression*> matchingNodes;

    // Helper to try finding an entity by name and add matching nodes
    const auto tryFindEntity = [&](auto getEntityId) {
      try {
        if (auto* exprs =
                folly::get_ptr(entityIdToNodes_, toEntityId(getEntityId()))) {
          matchingNodes.insert(
              matchingNodes.end(), exprs->begin(), exprs->end());
        }
      } catch (const std::out_of_range&) {
      }
    };

    tryFindEntity([&] { return universe_->getContainerId(searchQuery); });
    tryFindEntity([&] { return universe_->getObjectId(searchQuery); });

    if (matchingNodes.empty()) {
      XLOG(INFO) << "getTreeNode called with search query: "
                 << *request.search()->query() << " not found";
    } else {
      // Single BFS to find all reachable ancestors
      relatedNodes = getReachableAncestors(matchingNodes);
      XLOG(INFO) << "getTreeNode called with search query: "
                 << *request.search()->query() << " found";
    }
  }

  const auto computeBoundAndAddToProperties = [](const Expression* expr,
                                                 ExpressionProperties&
                                                     properties,
                                                 Context& context) {
    try {
      const auto [lb, ub] = expr->lowerAndUpperBounds(context);
      properties.properties()->emplace(
          "lower bound", PropertiesHelper::makeDoubleValue(lb));
      properties.properties()->emplace(
          "upper bound", PropertiesHelper::makeDoubleValue(ub));
    } catch (const std::exception& e) {
      XLOGF(
          INFO,
          "Not adding lower and upper bounds for expression of type '{}' because of error: {}",
          expr->getType(),
          e.what());
    }
  };

  auto expression = expressionIdToPtr_.at(*request.expressionId());
  Context sourceContext;
  sourceContext.changes() = getChangesFromInitial(*request.sourceAssignment());

  Context destinationContext;
  destinationContext.changes() =
      getChangesFromInitial(*request.destinationAssignment());

  auto& orchestrator = problem_->getOrchestrator();

  TreeNodeResponse result;
  result.node()->expressionId() = expression->getId();
  result.node()->expressionType() = expression->getType();
  result.node()->description() = expression->description;
  result.node()->sourceValue() =
      orchestrator.evaluate(expression, sourceContext);
  result.node()->destinationValue() =
      orchestrator.evaluate(expression, destinationContext);
  result.node()->coefficient() = 1.0;

  auto& properties = *result.node()->properties();
  properties = replaceIdsWithNames(expression->getProperties());
  computeBoundAndAddToProperties(expression, properties, sourceContext);

  const bool filterBySearch =
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          request.search()) &&
      request.search()->query().has_value() &&
      !request.search()->query().value().empty();

  for (const auto& child : expression->children()) {
    if (filterBySearch && !relatedNodes.contains(child->getId())) {
      continue;
    }
    TreeNode node;
    node.expressionId() = child->getId();
    node.expressionType() = child->getType();
    node.description() = child->description;
    node.sourceValue() = orchestrator.evaluate(child.get(), sourceContext);
    node.destinationValue() =
        orchestrator.evaluate(child.get(), destinationContext);
    node.coefficient() = expression->getChildCoefficient(child.get());
    auto& childProperties = *node.properties();
    childProperties = replaceIdsWithNames(child->getProperties());
    computeBoundAndAddToProperties(child.get(), childProperties, sourceContext);

    result.children()->push_back(std::move(node));
  }

  auto getMetric = [&request](const TreeNode& node) -> double {
    auto metric = *request.childrenOrderMetric();
    const double coefficient = *node.coefficient();
    if (metric == TreeNodeOrderMetric::SOURCE_VALUE) {
      return coefficient * *node.sourceValue();
    }
    if (metric == TreeNodeOrderMetric::DESTINATION_VALUE) {
      return coefficient * *node.destinationValue();
    }
    if (metric == TreeNodeOrderMetric::DELTA_VALUE) {
      return coefficient * (*node.destinationValue() - *node.sourceValue());
    }
    throw std::runtime_error("unknown node order metric");
  };

  // Sort children by metric in ascending order.
  std::sort(
      result.children()->begin(),
      result.children()->end(),
      [&getMetric](const TreeNode& node1, const TreeNode& node2) {
        return getMetric(node1) < getMetric(node2);
      });

  // Reverse order if descending order is requested.
  if (*request.childrenOrderDirection() == OrderDirection::DESCENDING) {
    std::reverse(result.children()->begin(), result.children()->end());
  }

  // Apply pagination.
  const size_t firstItem = std::min(
      result.children()->size(), size_t(*request.childrenPage()->offset()));
  const size_t lastItem = std::min(
      result.children()->size(), firstItem + *request.childrenPage()->limit());
  result.children() = {
      result.children()->begin() + firstItem,
      result.children()->begin() + lastItem};

  return result;
}

void ModelServer::initExpressionIdToPtr() {
  const std::vector<Expression*> roots;
  for (auto& [_, expression] : materialized_->hardConstraints) {
    initExpressionIdToPtr(expression.get());
  }
  for (auto& [_, expression] : materialized_->softConstraints) {
    initExpressionIdToPtr(expression.get());
  }
  for (auto& [_, expression] : materialized_->userGoals) {
    initExpressionIdToPtr(expression.get());
  }
}

void ModelServer::initExpressionIdToPtr(Expression* expression) {
  auto expressionId = expression->getId();
  if (!expressionIdToPtr_.insert(std::make_pair(expressionId, expression))
           .second) {
    // This expression has been initialized before.
    return;
  }
  for (const auto& child : expression->children()) {
    initExpressionIdToPtr(child.get());
  }
}

namespace {
// Process a single node's properties and return entity-to-node pairs
std::vector<std::pair<EntityId, Expression*>> getNodeEntityPairs(
    Expression* node) {
  std::vector<std::pair<EntityId, Expression*>> entries;
  const auto properties = node->getProperties();
  const auto& propsMap = *properties.properties();

  for (const auto& [propName, propValue] : propsMap) {
    // Check for single container ID
    if (propValue.valueContainerId().has_value()) {
      entries.emplace_back(
          EntityId(propValue.valueContainerId()->value().value()), node);
    }

    // Check for container ID list
    if (propValue.valueContainerIdList().has_value()) {
      for (auto containerInt :
           propValue.valueContainerIdList()->value().value()) {
        entries.emplace_back(EntityId(containerInt), node);
      }
    }

    // Check for single object ID
    if (propValue.valueObjectId().has_value()) {
      entries.emplace_back(
          EntityId(propValue.valueObjectId()->value().value()), node);
    }

    // Check for object ID to double map (keys are object IDs)
    if (propValue.valueObjectIdDoubleMap().has_value()) {
      for (const auto& [objectInt, _] :
           propValue.valueObjectIdDoubleMap()->value().value()) {
        entries.emplace_back(EntityId(objectInt), node);
      }
    }
  }
  return entries;
}
} // namespace

// Coroutine that processes all nodes using CoroUtils batching
static folly::coro::Task<void> initPropertiesIndexCoro(
    const std::vector<Expression*>& postOrder,
    entities::Map<explorer::EntityId, std::vector<Expression*>>&
        entityIdToNodes) {
  using EntryVec = std::vector<std::pair<explorer::EntityId, Expression*>>;

  auto result = co_await CoroUtils::runEachAndGetAccumulatedWithBatching(
      postOrder,
      [](auto it) -> EntryVec { return getNodeEntityPairs(*it); },
      [](EntryVec& accumulated, const EntryVec& batch) {
        accumulated.insert(accumulated.end(), batch.begin(), batch.end());
      });

  // Populate the map from accumulated results
  for (auto& [entityId, node] : result) {
    entityIdToNodes[entityId].push_back(node);
  }
  co_return;
}

void ModelServer::initPropertiesIndex() {
  // Traverse all nodes in postOrder and check their properties for containers
  // and objects. Process nodes in parallel batches for better performance.
  const auto& postOrder = problem_->getOrchestrator().getNodesInPostorder();

  // Use blocking wait with a CPU executor. CoroUtils will automatically
  // compute batch sizes based on the number of threads available.
  auto executor = std::make_shared<folly::CPUThreadPoolExecutor>(
      folly::available_concurrency());
  folly::coro::blockingWait(
      folly::coro::co_withExecutor(
          executor.get(),
          initPropertiesIndexCoro(postOrder, entityIdToNodes_)));
}

void ModelServer::startPropertiesIndexAsync() {
  const auto& postOrder = problem_->getOrchestrator().getNodesInPostorder();
  propertiesIndexFuture_ = co_withExecutor(
                               executor_.get(),
                               folly::coro::co_invoke(
                                   [this, postOrder = std::cref(postOrder)]()
                                       -> folly::coro::Task<folly::Unit> {
                                     co_await initPropertiesIndexCoro(
                                         postOrder.get(), entityIdToNodes_);
                                     co_return folly::unit;
                                   }))
                               .start();
}

void ModelServer::waitForPropertiesIndex() const {
  folly::call_once(propertiesIndexOnceFlag_, [this]() {
    if (propertiesIndexFuture_.valid()) {
      std::move(propertiesIndexFuture_).get();
    }
  });
}

void ModelServer::runMetricsAndObjectiveInit() {
  // Both functions below mutate Expressions since they both call
  // fullApply/partialApply(). Therefore, they must run serially to avoid race
  // conditions.
  if (materialized_->metrics) {
    materialized_->metrics->fullApply(initialAssignment_);
  }
  initObjectiveNameToExpr();
}

void ModelServer::startMetricsAndObjectiveInitAsync() {
  metricsAndObjectiveInitFuture_ = folly::via(executor_.get(), [this]() {
                                     runMetricsAndObjectiveInit();
                                   }).semi();
}

void ModelServer::waitForMetricsAndObjectiveInit() const {
  folly::call_once(metricsAndObjectiveInitOnceFlag_, [this]() {
    if (metricsAndObjectiveInitFuture_.valid()) {
      std::move(metricsAndObjectiveInitFuture_).get();
    }
  });
}

ExpressionProperties ModelServer::replaceIdsWithNames(
    ExpressionProperties properties) const {
  for (auto& [name, value] : *properties.properties()) {
    value = replaceIdsWithNames(std::move(value));
  }
  return properties;
}

ExpressionPropertyValue ModelServer::replaceIdsWithNames(
    ExpressionPropertyValue value) const {
  switch (value.getType()) {
    case ExpressionPropertyValue::Type::valueContainerIdList:
      return replaceIdsWithNames(std::move(*value.valueContainerIdList()));
    case ExpressionPropertyValue::Type::valueObjectIdDoubleMap:
      return replaceIdsWithNames(std::move(*value.valueObjectIdDoubleMap()));
    case ExpressionPropertyValue::Type::valueObjectId:
      return replaceIdsWithNames(std::move(*value.valueObjectId()));
    case ExpressionPropertyValue::Type::valueContainerId:
      return replaceIdsWithNames(std::move(*value.valueContainerId()));
    default:
      return value;
  }
}

ExpressionPropertyValue ModelServer::replaceIdsWithNames(
    ExpressionPropertyValueContainerIdList value) const {
  ExpressionPropertyValueContainerNameList valueContainerNameList;
  valueContainerNameList.value()->reserve(value.value()->size());
  for (const int containerInt : *value.value()) {
    auto containerId = entities::ContainerId(containerInt);
    auto& containerName = universe_->getEntityName(containerId);
    valueContainerNameList.value()->push_back(containerName);
  }
  ExpressionPropertyValue newValue;
  newValue.valueContainerNameList() = std::move(valueContainerNameList);
  return newValue;
}

ExpressionPropertyValue ModelServer::replaceIdsWithNames(
    ExpressionPropertyValueObjectIdDoubleMap value) const {
  ExpressionPropertyValueObjectNameDoubleMap valueObjectNameDoubleMap;
  for (auto& [objectInt, objectValue] : *value.value()) {
    auto objectId = entities::ObjectId(objectInt);
    auto& objectName = universe_->getEntityName(objectId);
    valueObjectNameDoubleMap.value()[objectName] = objectValue;
  }
  ExpressionPropertyValue newValue;
  newValue.valueObjectNameDoubleMap() = std::move(valueObjectNameDoubleMap);
  return newValue;
}

ExpressionPropertyValue ModelServer::replaceIdsWithNames(
    ExpressionPropertyValueObjectId value) const {
  ExpressionPropertyValueObjectName valueObjectName;
  auto objectId = entities::ObjectId(*value.value());
  auto& objectName = universe_->getEntityName(objectId);
  valueObjectName.value() = objectName;
  ExpressionPropertyValue newValue;
  newValue.valueObjectName() = std::move(valueObjectName);
  return newValue;
}

ExpressionPropertyValue ModelServer::replaceIdsWithNames(
    ExpressionPropertyValueContainerId value) const {
  ExpressionPropertyValueContainerName valueContainerName;
  auto containerId = entities::ContainerId(*value.value());
  auto& containerName = universe_->getEntityName(containerId);
  valueContainerName.value() = containerName;
  ExpressionPropertyValue newValue;
  newValue.valueContainerName() = std::move(valueContainerName);
  return newValue;
}

folly::coro::Task<ExportTableResponse> ModelServer::exportTable(
    const ExportTableRequest& request) const {
  waitForTableData();
  const auto& tableName = *request.tableName();
  std::optional<Table> tableFromPromise;
  const Table* tablePtr = nullptr;

  if (auto promisePtr = folly::get_ptr(tablePromises_, tableName)) {
    tableFromPromise = co_await (*promisePtr)->getSemiFuture();
    tablePtr = &(*tableFromPromise);
  } else {
    auto metricCollectionPtr =
        folly::get_ptr(metricCollectionNameToType_, tableName);
    if (metricCollectionPtr) {
      if (!request.assignmentA().has_value() ||
          !request.assignmentB().has_value()) {
        throw std::runtime_error(
            fmt::format(
                "Evaluating metric collection '{}' requires both assignment to be set in ExportTableRequest",
                tableName));
      }
      tablePtr = &tablulateMetricCollection(
          *metricCollectionPtr,
          request.assignmentA().value(),
          request.assignmentB().value());
    }
  }

  if (!tablePtr) {
    throw std::runtime_error(fmt::format("Unknown table {} ", tableName));
  }

#ifdef REBALANCER_OSS_BUILD
  throw std::runtime_error(
      "Exporting tables to Presto is not supported in OSS Explorer");
#else
  co_return exportTableRequestToResponseCache_.getSavedOrCompute(request, [&]() {
    auto requestUUID = UuidGenerator::genString();
    auto timeNow = std::time(nullptr);
    auto prestoTableName = PrestoUtils::getSanitizedOrThrow(
        fmt::format(
            "{}_{}_{}",
            *request.tableName(),
            std::move(requestUUID),
            std::move(timeNow)));

    PrestoUtils::createPrestoTableAndInsertRows(
        *tablePtr, prestoTableName, kPrestoNamespace, kRetentionDays);
    auto prestoUrl = fmt::format(
        "https://www.internalfb.com/intern/scuba/query/?dataset={}&pool=presto:{}&view=samples_client",
        prestoTableName,
        kPrestoNamespace);
    XLOGF(
        INFO,
        "Exported table {}; presto URL: {}",
        *request.tableName(),
        prestoUrl);

    ExportTableResponse response;
    response.tableName() = std::move(prestoTableName);
    response.tableNamespace() = kPrestoNamespace;
    response.urlToTable() = std::move(prestoUrl);
    return response;
  });
#endif
}

int ModelServer::getExpressionCount() const {
  return expressionIdToPtr_.size();
}

} // namespace facebook::rebalancer::explorer
