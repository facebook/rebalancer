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

#pragma once

#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>

#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/io/IOBufQueue.h>
#include <folly/SocketAddress.h>

#include <chrono>
#include <memory>
#include <string>

namespace facebook::rebalancer::explorer::proxy {

// Runtime configuration shared by every request. Owned by the factory and
// stable for the lifetime of the server, so handlers hold it by const-ref.
struct ProxyConfig {
  // Address of the single, configured Explorer backend the proxy always dials,
  // resolved once at startup so per-request dialing never blocks an IO thread
  // on DNS. The caller-supplied `Handle` is never used to pick a destination
  // (SSRF-safe); it is only forwarded as an RPC argument.
  folly::SocketAddress backendAddress;

  // Bearer token required on every `/v2/*` request. Empty only when
  // `authDisabled` is true (the binary fails closed otherwise).
  std::string authToken;
  bool authDisabled{false};

  // Per-call backend timeout (connect + receive).
  std::chrono::milliseconds backendTimeout{std::chrono::milliseconds(30000)};

  // Shared IO executor used to run the backend Thrift calls. Each request
  // pins to one of its EventBases for the duration of the call.
  std::shared_ptr<folly::IOThreadPoolExecutor> backendExecutor;
};

// One instance per in-flight request. Buffers the request body, then on EOM
// authenticates, routes `/v2/<method>` to the matching backend RPC, and
// transcodes JSON <-> Thrift. `GET /healthz` is served without auth.
class JsonProxyHandler : public proxygen::RequestHandler {
 public:
  explicit JsonProxyHandler(const ProxyConfig& config);

  void onRequest(
      std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override;
  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;
  void onEOM() noexcept override;
  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;
  void requestComplete() noexcept override;
  void onError(proxygen::ProxygenError err) noexcept override;

 private:
  // Returns true if the request carries a valid `Authorization: Bearer` token
  // matching the configured one (constant-time comparison).
  bool isAuthorized() const;

  // Kicks off the backend RPC for `method` on the shared IO executor and wires
  // the result back onto this handler's EventBase as an HTTP response.
  void dispatch(std::string method);

  void sendJson(int status, std::string body);
  void sendError(int status, const std::string& message);

  // Deletes this handler once proxygen is done with it and no backend call is
  // still in flight. Always called on the handler's EventBase.
  void maybeDelete();

  const ProxyConfig& config_;
  std::unique_ptr<proxygen::HTTPMessage> request_;
  folly::IOBufQueue body_{folly::IOBufQueue::cacheChainLength()};

  // Lifetime/state flags. All mutated only on the handler's EventBase.
  bool asyncOutstanding_{false};
  bool proxygenDone_{false};
  bool clientGone_{false};
};

// Creates a JsonProxyHandler per request, all sharing the same ProxyConfig.
class JsonProxyHandlerFactory : public proxygen::RequestHandlerFactory {
 public:
  explicit JsonProxyHandlerFactory(ProxyConfig config);

  void onServerStart(folly::EventBase* evb) noexcept override;
  void onServerStop() noexcept override;
  proxygen::RequestHandler* onRequest(
      proxygen::RequestHandler* handler,
      proxygen::HTTPMessage* msg) noexcept override;

 private:
  ProxyConfig config_;
};

} // namespace facebook::rebalancer::explorer::proxy
