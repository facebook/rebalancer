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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_XPRESS

// Single entry point for the Xpress 9.9.0 typed C++ API.
//
// The vendor headers cannot simply be included: xpress.hpp appends its own
// implementation (xpress.cpp) to itself, and that implementation dereferences
// xpress::objects::XpressProblem. But xpress_objects.hpp -- which defines
// XpressProblem -- includes xpress.hpp up front, so the implementation gets
// parsed while XpressProblem is still only forward-declared, and the build
// fails.
//
// Suppressing the auto-includes and pulling the implementations in afterwards
// fixes the ordering. The definitions are all `inline`, so including them from
// every translation unit is well-formed and the linker merges them; the
// alternative (declarations only, implementations in one TU) trips
// -Wundefined-inline in every other TU.
//
// clang-format must not touch this block: it sorts the includes alphabetically,
// which puts the implementations before the declarations and reintroduces the
// ordering bug.
// clang-format off
#define XPRESS_DO_NOT_INCLUDE_CXX_SOURCE
#include <xpress_objects.hpp>
#include <xpress.cpp>
#include <xpress_objects.cpp>
// clang-format on

#endif // REBALANCER_USE_XPRESS
