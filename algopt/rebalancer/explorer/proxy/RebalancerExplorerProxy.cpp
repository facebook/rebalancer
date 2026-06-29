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

// Standalone HTTP/JSON front door for the Thrift RebalancerExplorerService.
//
// External callers POST /v2/<method> with a JSON body; this binary transcodes
// JSON <-> Thrift and forwards each call to a single configured Explorer
// backend over fbthrift/Rocket. It exists so the Explorer BFF can reach the
// backend from outside Meta, where the ServiceRouter path is unavailable.

#include "rebalancer/explorer/proxy/JsonProxyHandler.h"

#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>

#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/init/Init.h>
#include <folly/logging/xlog.h>
#include <folly/portability/GFlags.h>
#include <folly/SocketAddress.h>
#include <folly/system/HardwareConcurrency.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>
#include <vector>

DEFINE_int32(port, 8081, "Port the proxy listens on (bound on 0.0.0.0).");
DEFINE_string(
    backend_host,
    "127.0.0.1",
    "Host of the Explorer Thrift backend the proxy forwards to.");
DEFINE_int32(
    backend_port,
    8080,
    "Thrift port of the Explorer backend the proxy forwards to.");
DEFINE_string(
    auth_token,
    "",
    "Bearer token required on /v2/* requests. If empty, the proxy reads the "
    "REBALANCER_PROXY_AUTH_TOKEN environment variable instead.");
DEFINE_bool(
    disable_auth,
    false,
    "Disable bearer-token authentication entirely. DANGEROUS; intended only "
    "for local development.");
DEFINE_int32(
    http_threads,
    0,
    "Number of HTTP server threads. 0 means use hardware concurrency.");
DEFINE_int32(
    backend_threads,
    4,
    "Number of IO threads used to issue backend Thrift calls.");
DEFINE_int32(
    backend_timeout_ms,
    30000,
    "Per-call backend timeout (connect + receive) in milliseconds.");

namespace {

using facebook::rebalancer::explorer::proxy::JsonProxyHandlerFactory;
using facebook::rebalancer::explorer::proxy::ProxyConfig;

// Resolves the auth token from --auth_token, falling back to the
// REBALANCER_PROXY_AUTH_TOKEN environment variable.
std::string resolveAuthToken() {
  if (!FLAGS_auth_token.empty()) {
    return FLAGS_auth_token;
  }
  if (const char* env = std::getenv("REBALANCER_PROXY_AUTH_TOKEN")) {
    return std::string(env);
  }
  return std::string();
}

size_t resolveHttpThreads() {
  if (FLAGS_http_threads > 0) {
    return static_cast<size_t>(FLAGS_http_threads);
  }
  const auto hw = folly::available_concurrency();
  return hw > 0 ? static_cast<size_t>(hw) : 1;
}

} // namespace

int main(int argc, char* argv[]) {
  // OSS-portable init: this binary builds both in fbcode (Buck) and in the OSS
  // export (CMake), so it must not depend on internal initFacebook().
  const folly::Init init(&argc, &argv);

  const std::string authToken = resolveAuthToken();
  // Fail closed: refuse to start unauthenticated unless explicitly opted out.
  if (!FLAGS_disable_auth && authToken.empty()) {
    XLOG(ERR)
        << "No auth token configured. Set --auth_token or the "
           "REBALANCER_PROXY_AUTH_TOKEN environment variable, or pass "
           "--disable_auth to run without authentication (not recommended).";
    return 1;
  }
  if (FLAGS_disable_auth) {
    XLOG(WARN)
        << "Authentication is DISABLED (--disable_auth); every caller is "
           "served without a bearer token.";
  }

  // Resolve the backend address once, here at startup, so per-request dialing
  // never runs a blocking DNS lookup on a backend IO thread. Fail closed if the
  // configured host can't be resolved.
  folly::SocketAddress backendAddress;
  try {
    backendAddress = folly::SocketAddress(
        FLAGS_backend_host,
        static_cast<uint16_t>(FLAGS_backend_port),
        /*allowNameLookup=*/true);
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to resolve backend address " << FLAGS_backend_host
              << ":" << FLAGS_backend_port << ": " << e.what();
    return 1;
  }

  ProxyConfig config{
      .backendAddress = std::move(backendAddress),
      .authToken = authToken,
      .authDisabled = FLAGS_disable_auth,
      .backendTimeout = std::chrono::milliseconds(FLAGS_backend_timeout_ms),
      .backendExecutor = std::make_shared<folly::IOThreadPoolExecutor>(
          std::max(1, FLAGS_backend_threads)),
  };

  proxygen::HTTPServerOptions options;
  options.threads = resolveHttpThreads();
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories =
      proxygen::RequestHandlerChain()
          .addThen<JsonProxyHandlerFactory>(std::move(config))
          .build();

  const std::vector<proxygen::HTTPServer::IPConfig> ips = {
      {folly::SocketAddress("0.0.0.0", FLAGS_port, /*allowNameLookup=*/true),
       proxygen::HTTPServer::Protocol::HTTP},
  };

  proxygen::HTTPServer server(std::move(options));
  server.bind(ips);

  XLOG(INFO) << "Rebalancer Explorer JSON proxy listening on 0.0.0.0:"
             << FLAGS_port << ", forwarding to " << FLAGS_backend_host << ":"
             << FLAGS_backend_port;

  std::thread serverThread([&]() { server.start(); });
  serverThread.join();
  return 0;
}
