// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"

#include <folly/compression/Compression.h>
#include <folly/json/json.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <string>

namespace facebook {
namespace rebalancer {
namespace interface {

class Serializer {
 public:
  template <class T>
  static std::string serialize(
      const T& problem,
      bool shouldPrettifySerializedJson = true) {
    std::string serialized;
    apache::thrift::SimpleJSONSerializer{}.serialize(problem, &serialized);
    if (shouldPrettifySerializedJson) {
      return prettifyJson(serialized);
    }
    return serialized;
  }

  template <class T>
  static T deserialize(const std::string& serialized) {
    T problem;
    apache::thrift::SimpleJSONSerializer{}.deserialize(serialized, problem);
    return problem;
  }

  // zstd-compressed Thrift Binary: the same format used to persist bundles to
  // Manifold. Prefer over the SimpleJSON methods for bundles.
  template <class T>
  static std::string serializeBinaryZstd(const T& value) {
    std::string binary;
    apache::thrift::BinarySerializer{}.serialize(value, &binary);
    const auto codec =
        folly::compression::getCodec(folly::compression::CodecType::ZSTD);
    return codec->compress(binary);
  }

  template <class T>
  static T deserializeBinaryZstd(const std::string& compressed) {
    const auto codec =
        folly::compression::getCodec(folly::compression::CodecType::ZSTD);
    const std::string binary = codec->uncompress(compressed);
    T value;
    apache::thrift::BinarySerializer{}.deserialize(binary, value);
    return value;
  }

 private:
  static std::string prettifyJson(const std::string& json) {
    folly::json::serialization_opts opts;
    opts.double_fallback = true;
    return folly::toPrettyJson(folly::parseJson(json, opts));
  }
};

} // namespace interface
} // namespace rebalancer
} // namespace facebook
