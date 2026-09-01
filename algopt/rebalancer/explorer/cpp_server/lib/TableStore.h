// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/entities/Map.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <folly/coro/SharedPromise.h>
#include <folly/coro/Task.h>
#include <folly/Executor.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace facebook::rebalancer::explorer {

class TableStore {
 public:
  using BuildTaskMap = entities::Map<std::string, folly::coro::Task<Table>>;

  TableStore(
      std::shared_ptr<folly::Executor> executor,
      BuildTaskMap tableNameToBuildTask);
  folly::coro::Task<const Table&> get(const std::string& tableName) const;
  folly::coro::Task<const Table*> tryGet(const std::string& tableName) const;

 private:
  struct Entry {
    explicit Entry(folly::coro::Task<Table> task)
        : buildTask(std::move(task)) {}

    folly::coro::Task<Table> buildTask;
    std::atomic<bool> buildStarted{false};
    folly::coro::SharedPromise<std::shared_ptr<const Table>> table;
  };

  std::shared_ptr<folly::Executor> executor_;
  entities::Map<std::string, std::unique_ptr<Entry>> tableNameToEntry_;
};

} // namespace facebook::rebalancer::explorer
