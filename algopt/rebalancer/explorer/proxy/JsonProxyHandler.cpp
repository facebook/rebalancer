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

#include "rebalancer/explorer/proxy/JsonProxyHandler.h"

#include "rebalancer/explorer/if/gen-cpp2/explorer_clients.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"
// Pulls in the .tcc template definitions so SimpleJSONProtocol
// (de)serialization of the generated types links (only Binary/Compact are
// instantiated by default).
#include "rebalancer/explorer/if/gen-cpp2/explorer_types_custom_protocol.h"

#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/HTTPMethod.h>

#include <fmt/core.h>
#include <folly/coro/Task.h>
#include <folly/dynamic.h>
#include <folly/Executor.h>
#include <folly/futures/Future.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/json.h>
#include <folly/logging/xlog.h>
#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/Try.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include <thrift/lib/cpp2/async/RpcOptions.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace facebook::rebalancer::explorer::proxy {

namespace {

// Pull in the generated Thrift types (Handle, *Request, *Response, ...) and the
// service tag so the dispatch table below reads cleanly.
using namespace facebook::rebalancer::explorer;

using apache::thrift::RpcOptions;
using apache::thrift::SimpleJSONSerializer;
using ExplorerClient = apache::thrift::Client<
    facebook::rebalancer::explorer::RebalancerExplorerService>;

// Error carrying an HTTP status. Thrown by argument parsing/routing and mapped
// to a JSON error response. The message is safe to return to the caller (it
// describes the request, never host/transport internals).
class ProxyHttpError : public std::runtime_error {
 public:
  ProxyHttpError(int status, const std::string& message)
      : std::runtime_error(message), status_(status) {}
  int status() const noexcept {
    return status_;
  }

 private:
  int status_;
};

constexpr folly::StringPiece kBearerPrefix{"Bearer "};
constexpr folly::StringPiece kV2Prefix{"/v2/"};
constexpr folly::StringPiece kHealthPath{"/healthz"};

const char* reasonPhrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 500:
      return "Internal Server Error";
    case 502:
      return "Bad Gateway";
    default:
      return "Error";
  }
}

// Length- and content-independent comparison to avoid leaking the token via
// response timing.
bool constantTimeEquals(folly::StringPiece a, folly::StringPiece b) {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  }
  return diff == 0;
}

// Deserializes one named field of the request body into a Thrift struct.
template <typename T>
T parseArg(const folly::dynamic& body, folly::StringPiece name) {
  const auto* field = body.get_ptr(name);
  if (field == nullptr) {
    throw ProxyHttpError(
        400, fmt::format("missing required field: '{}'", name));
  }
  try {
    return SimpleJSONSerializer::deserialize<T>(folly::toJson(*field));
  } catch (const std::exception&) {
    throw ProxyHttpError(
        400, fmt::format("invalid value for field: '{}'", name));
  }
}

template <typename T>
std::string toJson(const T& obj) {
  return SimpleJSONSerializer::serialize<std::string>(obj);
}

// One entry per Thrift method. Each parses its named arguments out of the JSON
// body, awaits the backend RPC, and serializes the response back to JSON.
// Stateless so they can be stored as plain function pointers.
using MethodFn = folly::coro::Task<std::string> (*)(
    ExplorerClient&,
    const folly::dynamic&,
    RpcOptions&);

folly::coro::Task<std::string>
mGetHandle(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto request = parseArg<HandleRequest>(b, "request");
  co_return toJson(co_await c.co_getHandle(o, request));
}

folly::coro::Task<std::string>
mGetSandboxStatus(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  co_return toJson(co_await c.co_getSandboxStatus(o, handle));
}

folly::coro::Task<std::string> mGetProblemMetadataV2(
    ExplorerClient& c,
    const folly::dynamic& b,
    RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  co_return toJson(co_await c.co_getProblemMetadataV2(o, handle));
}

folly::coro::Task<std::string>
mGetDataV2(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<DataRequest>(b, "request");
  co_return toJson(co_await c.co_getDataV2(o, handle, request));
}

folly::coro::Task<std::string>
mEvaluateV2(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<EvaluateRequest>(b, "request");
  co_return toJson(co_await c.co_evaluateV2(o, handle, request));
}

folly::coro::Task<std::string> mEvaluateMetricCollection(
    ExplorerClient& c,
    const folly::dynamic& b,
    RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<DataRequest>(b, "request");
  auto evaluateRequestA = parseArg<EvaluateRequest>(b, "evaluateRequestA");
  auto evaluateRequestB = parseArg<EvaluateRequest>(b, "evaluateRequestB");
  co_return toJson(
      co_await c.co_evaluateMetricCollection(
          o, handle, request, evaluateRequestA, evaluateRequestB));
}

folly::coro::Task<std::string>
mGetGoalSpecV2(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<GoalSpecRequest>(b, "request");
  co_return toJson(co_await c.co_getGoalSpecV2(o, handle, request));
}

folly::coro::Task<std::string> mGetConstraintSpecV2(
    ExplorerClient& c,
    const folly::dynamic& b,
    RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<ConstraintSpecRequest>(b, "request");
  co_return toJson(co_await c.co_getConstraintSpecV2(o, handle, request));
}

folly::coro::Task<std::string>
mGetTypeaheadV2(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<TypeaheadRequest>(b, "request");
  co_return toJson(co_await c.co_getTypeaheadV2(o, handle, request));
}

folly::coro::Task<std::string> mGetMovesBetweenAssignmentsV2(
    ExplorerClient& c,
    const folly::dynamic& b,
    RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<MovesBetweenAssignmentsRequest>(b, "request");
  co_return toJson(
      co_await c.co_getMovesBetweenAssignmentsV2(o, handle, request));
}

folly::coro::Task<std::string>
mGetTreeNodeV2(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<TreeNodeRequest>(b, "request");
  co_return toJson(co_await c.co_getTreeNodeV2(o, handle, request));
}

folly::coro::Task<std::string> mGetMetricDistributionV2(
    ExplorerClient& c,
    const folly::dynamic& b,
    RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<MetricDistributionRequest>(b, "request");
  co_return toJson(co_await c.co_getMetricDistributionV2(o, handle, request));
}

folly::coro::Task<std::string> mGetLocalSearchProfilesV2(
    ExplorerClient& c,
    const folly::dynamic& b,
    RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  co_return toJson(co_await c.co_getLocalSearchProfilesV2(o, handle));
}

folly::coro::Task<std::string>
mGetMoveSets(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<MoveSetsRequest>(b, "request");
  co_return toJson(co_await c.co_getMoveSets(o, handle, request));
}

folly::coro::Task<std::string>
mEditProblemV2(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<EditProblemRequest>(b, "request");
  co_return toJson(co_await c.co_editProblemV2(o, handle, request));
}

folly::coro::Task<std::string>
mExportTable(ExplorerClient& c, const folly::dynamic& b, RpcOptions& o) {
  auto handle = parseArg<Handle>(b, "handle");
  auto request = parseArg<ExportTableRequest>(b, "request");
  co_return toJson(co_await c.co_exportTable(o, handle, request));
}

const std::unordered_map<std::string, MethodFn>& methodTable() {
  static const auto* table = new std::unordered_map<std::string, MethodFn>{
      {"getHandle", &mGetHandle},
      {"getSandboxStatus", &mGetSandboxStatus},
      {"getProblemMetadataV2", &mGetProblemMetadataV2},
      {"getDataV2", &mGetDataV2},
      {"evaluateV2", &mEvaluateV2},
      {"evaluateMetricCollection", &mEvaluateMetricCollection},
      {"getGoalSpecV2", &mGetGoalSpecV2},
      {"getConstraintSpecV2", &mGetConstraintSpecV2},
      {"getTypeaheadV2", &mGetTypeaheadV2},
      {"getMovesBetweenAssignmentsV2", &mGetMovesBetweenAssignmentsV2},
      {"getTreeNodeV2", &mGetTreeNodeV2},
      {"getMetricDistributionV2", &mGetMetricDistributionV2},
      {"getLocalSearchProfilesV2", &mGetLocalSearchProfilesV2},
      {"getMoveSets", &mGetMoveSets},
      {"editProblemV2", &mEditProblemV2},
      {"exportTable", &mExportTable},
  };
  return *table;
}

// Runs entirely on `backendEvb`: builds a fresh client to the configured
// backend, parses the request body, and runs the matching RPC. Throws
// ProxyHttpError for client errors; other exceptions surface as backend
// failures.
folly::coro::Task<std::string> proxyCall(
    const ProxyConfig* config,
    folly::EventBase* backendEvb,
    std::string method,
    std::string requestBody) {
  const auto& table = methodTable();
  const auto it = table.find(method);
  if (it == table.end()) {
    throw ProxyHttpError(404, fmt::format("unknown method: '{}'", method));
  }

  folly::dynamic body = folly::dynamic::object();
  if (!requestBody.empty()) {
    try {
      body = folly::parseJson(requestBody);
    } catch (const std::exception&) {
      throw ProxyHttpError(400, "request body is not valid JSON");
    }
  }
  if (!body.isObject()) {
    throw ProxyHttpError(400, "request body must be a JSON object");
  }

  // Always dial the single configured backend; the caller's Handle is only ever
  // forwarded as an RPC argument, never used to choose a destination. The
  // address was resolved at startup, so no name lookup runs on this IO thread.
  auto socket = folly::AsyncSocket::newSocket(
      backendEvb,
      config->backendAddress,
      static_cast<uint32_t>(config->backendTimeout.count()));
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  channel->setTimeout(static_cast<uint32_t>(config->backendTimeout.count()));
  ExplorerClient client(std::move(channel));

  RpcOptions opts;
  opts.setTimeout(config->backendTimeout);

  co_return co_await it->second(client, body, opts);
}

} // namespace

JsonProxyHandler::JsonProxyHandler(const ProxyConfig& config)
    : config_(config) {}

void JsonProxyHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> headers) noexcept {
  request_ = std::move(headers);
}

void JsonProxyHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  body_.append(std::move(body));
}

void JsonProxyHandler::onUpgrade(proxygen::UpgradeProtocol) noexcept {}

void JsonProxyHandler::onEOM() noexcept {
  const auto path = request_->getPathAsStringPiece();

  // Liveness probe: no auth, no backend call.
  if (path == kHealthPath) {
    if (request_->getMethod() != proxygen::HTTPMethod::GET) {
      sendError(405, "method not allowed");
    } else {
      sendJson(200, R"({"status":"ok"})");
    }
    return;
  }

  if (request_->getMethod() != proxygen::HTTPMethod::POST) {
    sendError(405, "method not allowed");
    return;
  }
  if (!path.startsWith(kV2Prefix)) {
    sendError(404, "not found");
    return;
  }
  if (!config_.authDisabled && !isAuthorized()) {
    sendError(401, "unauthorized");
    return;
  }

  auto method = path.subpiece(kV2Prefix.size()).str();
  if (method.empty()) {
    sendError(404, "not found");
    return;
  }
  dispatch(std::move(method));
}

bool JsonProxyHandler::isAuthorized() const {
  const auto& authz = request_->getHeaders().getSingleOrEmpty(
      proxygen::HTTP_HEADER_AUTHORIZATION);
  const folly::StringPiece header{authz};
  if (!header.startsWith(kBearerPrefix)) {
    return false;
  }
  const auto token = header.subpiece(kBearerPrefix.size());
  return constantTimeEquals(token, config_.authToken);
}

void JsonProxyHandler::dispatch(std::string method) {
  auto* proxygenEvb = folly::EventBaseManager::get()->getEventBase();
  auto* backendEvb = config_.backendExecutor->getEventBase();

  std::string requestBody;
  if (!body_.empty()) {
    requestBody = body_.move()->moveToFbString().toStdString();
  }

  asyncOutstanding_ = true;
  folly::coro::co_withExecutor(
      folly::getKeepAliveToken(backendEvb),
      proxyCall(
          &config_, backendEvb, std::move(method), std::move(requestBody)))
      .start()
      .via(folly::getKeepAliveToken(proxygenEvb))
      .thenTry([this](folly::Try<std::string>&& result) {
        asyncOutstanding_ = false;
        if (clientGone_) {
          maybeDelete();
          return;
        }
        if (result.hasValue()) {
          sendJson(200, std::move(result.value()));
        } else if (
            auto* httpErr = result.tryGetExceptionObject<ProxyHttpError>()) {
          sendError(httpErr->status(), httpErr->what());
        } else if (
            auto* appErr = result.tryGetExceptionObject<
                           apache::thrift::TApplicationException>()) {
          // Application-level error from the backend service; the message is
          // the service's own and safe to forward.
          sendError(502, appErr->what());
        } else {
          // Transport/connection failures may embed host:port; keep the
          // response generic and log the detail server-side only.
          if (auto* ex = result.tryGetExceptionObject<std::exception>()) {
            XLOG(ERR) << "rebalancer explorer proxy upstream error: "
                      << ex->what();
          }
          sendError(502, "upstream request failed");
        }
        maybeDelete();
      });
}

void JsonProxyHandler::sendJson(int status, std::string body) {
  proxygen::ResponseBuilder(downstream_)
      .status(static_cast<uint16_t>(status), reasonPhrase(status))
      .header(proxygen::HTTP_HEADER_CONTENT_TYPE, "application/json")
      .body(std::move(body))
      .sendWithEOM();
}

void JsonProxyHandler::sendError(int status, const std::string& message) {
  const folly::dynamic obj = folly::dynamic::object("error", message);
  sendJson(status, folly::toJson(obj));
}

void JsonProxyHandler::maybeDelete() {
  if (proxygenDone_ && !asyncOutstanding_) {
    delete this;
  }
}

void JsonProxyHandler::requestComplete() noexcept {
  proxygenDone_ = true;
  maybeDelete();
}

void JsonProxyHandler::onError(proxygen::ProxygenError) noexcept {
  clientGone_ = true;
  proxygenDone_ = true;
  maybeDelete();
}

JsonProxyHandlerFactory::JsonProxyHandlerFactory(ProxyConfig config)
    : config_(std::move(config)) {}

void JsonProxyHandlerFactory::onServerStart(folly::EventBase*) noexcept {}

void JsonProxyHandlerFactory::onServerStop() noexcept {}

proxygen::RequestHandler* JsonProxyHandlerFactory::onRequest(
    proxygen::RequestHandler*,
    proxygen::HTTPMessage*) noexcept {
  return new JsonProxyHandler(config_);
}

} // namespace facebook::rebalancer::explorer::proxy
