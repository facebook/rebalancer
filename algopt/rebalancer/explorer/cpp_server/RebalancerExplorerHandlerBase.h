// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"
#include "rebalancer/explorer/if/gen-cpp2/RebalancerExplorerService.h"

#include <folly/coro/Task.h>
#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>

namespace facebook::rebalancer::explorer {

class TimeLogger {
 public:
  explicit TimeLogger(std::string name);

  TimeLogger(const TimeLogger& other) = delete;
  TimeLogger& operator=(const TimeLogger& other) = delete;
  TimeLogger(TimeLogger&& other) = delete;
  TimeLogger& operator=(TimeLogger&& other) = delete;

  ~TimeLogger();

 private:
  std::string name_;
  algopt::Timer timer_;
};

/**
 * Base class for explorer handlers.
 */
class RebalancerExplorerHandlerBase
    : public apache::thrift::ServiceHandler<RebalancerExplorerService> {
 protected:
  virtual folly::coro::Task<std::shared_ptr<const ModelServer>> getBackend(
      const Handle& handle) = 0;

  folly::coro::Task<std::unique_ptr<HandleResponse>> co_getHandle(
      std::unique_ptr<HandleRequest> request) override = 0;

  folly::coro::Task<std::unique_ptr<SandboxStatusResponse>> co_getSandboxStatus(
      std::unique_ptr<Handle> handle) override = 0;

  folly::coro::Task<std::unique_ptr<ProblemMetadataResponse>>
  co_getProblemMetadataV2(std::unique_ptr<Handle> handle) override;

  folly::coro::Task<std::unique_ptr<DataResponse>> co_getDataV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<DataRequest> request) override;

  folly::coro::Task<std::unique_ptr<EvaluateResponse>> co_evaluateV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<EvaluateRequest> request) override;

  folly::coro::Task<std::unique_ptr<Result>> co_evaluateMetricCollection(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<DataRequest> dataRequest,
      std::unique_ptr<EvaluateRequest> evaluateRequestA,
      std::unique_ptr<EvaluateRequest> evaluateRequestB) override;

  folly::coro::Task<std::unique_ptr<GoalSpecResponse>> co_getGoalSpecV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<GoalSpecRequest> request) override;

  folly::coro::Task<std::unique_ptr<ConstraintSpecResponse>>
  co_getConstraintSpecV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<ConstraintSpecRequest> request) override;

  folly::coro::Task<std::unique_ptr<TypeaheadResponse>> co_getTypeaheadV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<TypeaheadRequest> request) override;

  folly::coro::Task<std::unique_ptr<MovesBetweenAssignmentsResponse>>
  co_getMovesBetweenAssignmentsV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<MovesBetweenAssignmentsRequest> request) override;

  folly::coro::Task<std::unique_ptr<TreeNodeResponse>> co_getTreeNodeV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<TreeNodeRequest> request) override;

  folly::coro::Task<std::unique_ptr<MetricDistributionResponse>>
  co_getMetricDistributionV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<MetricDistributionRequest> request) override;

  folly::coro::Task<std::unique_ptr<LocalSearchProfilesResponse>>
  co_getLocalSearchProfilesV2(std::unique_ptr<Handle> handle) override;

  folly::coro::Task<std::unique_ptr<MoveSetsResponse>> co_getMoveSets(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<MoveSetsRequest> request) override;

  folly::coro::Task<std::unique_ptr<EditProblemResponse>> co_editProblemV2(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<EditProblemRequest> request) override;

  folly::coro::Task<std::unique_ptr<ExportTableResponse>> co_exportTable(
      std::unique_ptr<Handle> handle,
      std::unique_ptr<ExportTableRequest> request) override;
};

} // namespace facebook::rebalancer::explorer
