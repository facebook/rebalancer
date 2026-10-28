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

// Deliberately a second translation unit that pulls in the Xpress typed C++ API
// alongside XpressV2Types.cpp. The vendor ships its implementations as `inline`
// definitions in .cpp files that the headers include, so this file exists to
// keep us honest that including them from more than one TU still links.
#include "algopt/lp/detail/xpress_v2/xpress_v2.h" // NOLINT(facebook-unused-include-check)
