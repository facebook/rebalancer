// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/RebalancerExplorerHandlerBase.h"

#include "algopt/rebalancer/algopt_common/Timer.h"

#include <fmt/format.h>

namespace facebook::rebalancer::explorer {
TimeLogger::TimeLogger(std::string name)
    : name_(std::move(name)), timer_(/*autoStart=*/true) {}

TimeLogger::~TimeLogger() {
  XLOG(INFO) << fmt::format(
      "{} took {:.3f} seconds", name_, timer_.getSeconds());
}

folly::coro::Task<std::unique_ptr<ProblemMetadataResponse>>
RebalancerExplorerHandlerBase::co_getProblemMetadataV2(
    std::unique_ptr<Handle> handle) {
  [[maybe_unused]] const TimeLogger logger("getProblemMetadataV2");
  auto backend = co_await getBackend(*handle);
  auto response = std::make_unique<ProblemMetadataResponse>();
  response->metadata() = backend->getProblemMetadata();
  co_return std::move(response);
}

folly::coro::Task<std::unique_ptr<DataResponse>>
RebalancerExplorerHandlerBase::co_getDataV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<DataRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getDataV2");
  auto backend = co_await getBackend(*handle);
  auto response = std::make_unique<DataResponse>();
  response->result() = co_await backend->getData(*request->query());
  co_return std::move(response);
}

folly::coro::Task<std::unique_ptr<EvaluateResponse>>
RebalancerExplorerHandlerBase::co_evaluateV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<EvaluateRequest> request) {
  [[maybe_unused]] const TimeLogger logger("evaluateV2");
  auto backend = co_await getBackend(*handle);
  auto response = std::make_unique<EvaluateResponse>();
  response->result() = backend->evaluate(*request->assignment());
  co_return std::move(response);
}

folly::coro::Task<std::unique_ptr<Result>>
RebalancerExplorerHandlerBase::co_evaluateMetricCollection(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<DataRequest> dataRequest,
    std::unique_ptr<EvaluateRequest> evaluateRequestA,
    std::unique_ptr<EvaluateRequest> evaluateRequestB) {
  [[maybe_unused]] const TimeLogger logger("evaluateMetricCollection");
  auto backend = co_await getBackend(*handle);
  auto& assignmentA = *evaluateRequestA->assignment();
  auto& assignmentB = *evaluateRequestB->assignment();
  auto table = backend->evaluateMetricCollection(
      *dataRequest->query(), assignmentA, assignmentB);
  co_return std::make_unique<Result>(std::move(table));
}

folly::coro::Task<std::unique_ptr<GoalSpecResponse>>
RebalancerExplorerHandlerBase::co_getGoalSpecV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<GoalSpecRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getGoalSpecV2");
  auto backend = co_await getBackend(*handle);
  auto response = std::make_unique<GoalSpecResponse>();
  response->spec() = backend->getGoalSpec(*request->name());
  co_return std::move(response);
}

folly::coro::Task<std::unique_ptr<ConstraintSpecResponse>>
RebalancerExplorerHandlerBase::co_getConstraintSpecV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<ConstraintSpecRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getConstraintSpecV2");
  auto backend = co_await getBackend(*handle);
  auto response = std::make_unique<ConstraintSpecResponse>();
  response->spec() = backend->getConstraintSpec(*request->name());
  co_return std::move(response);
}

folly::coro::Task<std::unique_ptr<TypeaheadResponse>>
RebalancerExplorerHandlerBase::co_getTypeaheadV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<TypeaheadRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getTypeaheadV2");
  auto backend = co_await getBackend(*handle);
  auto result = co_await backend->getTypeahead(*request);
  co_return std::make_unique<TypeaheadResponse>(std::move(result));
}

folly::coro::Task<std::unique_ptr<MovesBetweenAssignmentsResponse>>
RebalancerExplorerHandlerBase::co_getMovesBetweenAssignmentsV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<MovesBetweenAssignmentsRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getMovesBetweenAssignmentsV2");
  auto backend = co_await getBackend(*handle);
  auto result = backend->getMovesBetweenAssignments(*request);
  co_return std::make_unique<MovesBetweenAssignmentsResponse>(
      std::move(result));
}

folly::coro::Task<std::unique_ptr<TreeNodeResponse>>
RebalancerExplorerHandlerBase::co_getTreeNodeV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<TreeNodeRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getTreeNodeV2");
  auto backend = co_await getBackend(*handle);
  auto result = backend->getTreeNode(*request);
  co_return std::make_unique<TreeNodeResponse>(std::move(result));
}

folly::coro::Task<std::unique_ptr<MetricDistributionResponse>>
RebalancerExplorerHandlerBase::co_getMetricDistributionV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<MetricDistributionRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getMetricDistributionV2");
  auto backend = co_await getBackend(*handle);
  auto result = co_await backend->getMetricDistribution(*request);
  co_return std::make_unique<MetricDistributionResponse>(std::move(result));
}

folly::coro::Task<std::unique_ptr<LocalSearchProfilesResponse>>
RebalancerExplorerHandlerBase::co_getLocalSearchProfilesV2(
    std::unique_ptr<Handle> handle) {
  [[maybe_unused]] const TimeLogger logger("getLocalSearchProfilesV2");
  auto backend = co_await getBackend(*handle);
  auto response = std::make_unique<LocalSearchProfilesResponse>();
  response->profiles() = backend->getLocalSearchProfiles();
  co_return std::move(response);
}

folly::coro::Task<std::unique_ptr<EditProblemResponse>>
RebalancerExplorerHandlerBase::co_editProblemV2(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<EditProblemRequest> request) {
  [[maybe_unused]] const TimeLogger logger("editProblemV2");
  auto backend = co_await getBackend(*handle);
  auto result = backend->editProblem(*request);
  co_return std::make_unique<EditProblemResponse>(std::move(result));
}

folly::coro::Task<std::unique_ptr<ExportTableResponse>>
RebalancerExplorerHandlerBase::co_exportTable(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<ExportTableRequest> request) {
  [[maybe_unused]] const TimeLogger logger("exportTable");
  auto backend = co_await getBackend(*handle);
  auto result = co_await backend->exportTable(*request);
  co_return std::make_unique<ExportTableResponse>(std::move(result));
}

folly::coro::Task<std::unique_ptr<MoveSetsResponse>>
RebalancerExplorerHandlerBase::co_getMoveSets(
    std::unique_ptr<Handle> handle,
    std::unique_ptr<MoveSetsRequest> request) {
  [[maybe_unused]] const TimeLogger logger("getMoveSets");
  auto backend = co_await getBackend(*handle);
  co_return std::make_unique<MoveSetsResponse>(backend->getMoveSets(*request));
}

} // namespace facebook::rebalancer::explorer
