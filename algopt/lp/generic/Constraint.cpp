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

#include "algopt/lp/generic/Constraint.h"

#include <fmt/format.h>
#include <folly/Demangle.h>

#include <stdexcept>
#include <string_view>
#include <typeinfo>

namespace facebook::algopt::lp {

namespace {
[[noreturn]] void unsupported(
    const ConstraintImpl& impl,
    std::string_view accessor) {
  throw std::runtime_error(
      fmt::format(
          "{}() is not supported by backend {}",
          accessor,
          folly::demangle(typeid(impl).name())));
}
} // namespace

ConstraintImpl::~ConstraintImpl() = default;

double ConstraintImpl::getDual() const {
  unsupported(*this, "getDual");
}

double ConstraintImpl::getSlack() const {
  unsupported(*this, "getSlack");
}

double ConstraintImpl::getRHS() const {
  unsupported(*this, "getRHS");
}

Constraint::Constraint(std::shared_ptr<ConstraintImpl> constraint)
    : constraint_(constraint) {}

std::shared_ptr<ConstraintImpl> Constraint::get() {
  return constraint_;
}

double Constraint::getDual() const {
  return constraint_->getDual();
}

double Constraint::getSlack() const {
  return constraint_->getSlack();
}

double Constraint::getRHS() const {
  return constraint_->getRHS();
}

} // namespace facebook::algopt::lp
