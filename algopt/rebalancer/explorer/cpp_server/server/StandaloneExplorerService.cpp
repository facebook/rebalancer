/*
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

#include "rebalancer/explorer/cpp_server/server/StandaloneExplorerServiceHandler.h"

#include <folly/init/Init.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/Init.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <climits>
#include <csignal>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

// HOST_NAME_MAX is not mandated by POSIX and is absent on some platforms
// (e.g. macOS, which uses _POSIX_HOST_NAME_MAX / MAXHOSTNAMELEN instead). Fall
// back to a conservative limit so the OSS build works on non-Linux targets.
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

DEFINE_string(xlog_config, "INFO", "Logging configuration");
DEFINE_int32(port, 8080, "Port to run Explorer on");
DEFINE_string(
    host,
    "",
    "Hostname advertised in Handle responses (defaults to the system hostname)");

namespace {

std::string resolveHost() {
  if (!FLAGS_host.empty()) {
    return FLAGS_host;
  }
  char buf[HOST_NAME_MAX + 1] = {};
  if (::gethostname(buf, sizeof(buf) - 1) == 0) {
    return std::string(buf);
  }
  return "localhost";
}

// Catches SIGINT/SIGTERM and asks the ThriftServer to stop, so serve() returns
// and in-flight requests drain. Registering these overrides folly::Init's fatal
// signal handler for them, so a normal shutdown / Ctrl-C no longer prints a
// backtrace.
class ShutdownSignalHandler : public folly::AsyncSignalHandler {
 public:
  ShutdownSignalHandler(
      folly::EventBase* evb,
      apache::thrift::ThriftServer* server)
      : folly::AsyncSignalHandler(evb), server_(server) {
    registerSignalHandler(SIGINT);
    registerSignalHandler(SIGTERM);
  }

  void signalReceived(int signum) noexcept override {
    XLOGF(INFO, "Received signal {}, shutting down gracefully", signum);
    server_->stop();
  }

 private:
  apache::thrift::ThriftServer* server_;
};

} // namespace

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);
  folly::initLoggingOrDie(FLAGS_xlog_config);

  const auto host = resolveHost();

  // Starts with no problem loaded. Clients load bundles on demand via
  // getHandle(), poll getSandboxStatus() until LOADED, then call endpoints.
  auto handler = std::make_shared<
      facebook::rebalancer::explorer::StandaloneExplorerServiceHandler>(
      host, FLAGS_port);

  auto server = std::make_shared<apache::thrift::ThriftServer>();
  server->setInterface(std::move(handler));
  server->setPort(FLAGS_port);
  server->setSSLPolicy(apache::thrift::SSLPolicy::DISABLED);
  server->setAllowPlaintextOnLoopback(true);

  // Drive an event base on a side thread purely to watch for shutdown signals;
  // serve() blocks the main thread until the handler calls server->stop().
  folly::EventBase signalEvb;
  ShutdownSignalHandler signalHandler(&signalEvb, server.get());
  std::thread signalThread([&signalEvb] { signalEvb.loopForever(); });

  XLOGF(
      INFO, "Rebalancer Explorer service listening on {}:{}", host, FLAGS_port);
  server->serve();

  signalEvb.terminateLoopSoon();
  signalThread.join();
  XLOG(INFO, "Rebalancer Explorer service stopped");
  return 0;
}
