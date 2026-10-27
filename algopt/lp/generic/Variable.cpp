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

#include "algopt/lp/generic/Variable.h"

#include <fmt/format.h>
#include <folly/Demangle.h>

#include <stdexcept>
#include <string_view>
#include <typeinfo>

namespace facebook::algopt::lp {

namespace {
[[noreturn]] void unsupported(
    const VariableImpl& impl,
    std::string_view accessor) {
  throw std::runtime_error(
      fmt::format(
          "{}() is not supported by backend {}",
          accessor,
          folly::demangle(typeid(impl).name())));
}
} // namespace

VariableImpl::~VariableImpl() = default;

void VariableImpl::setPartition(int /* unused */) {
  // no-op if variable partitioning is not supported by solver
}

bool VariableImpl::hasThreshold() const {
  return type_ == Type::SEMI_CONTINUOUS || type_ == Type::SEMI_INTEGER;
}

double VariableImpl::getReducedCost() const {
  unsupported(*this, "getReducedCost");
}

Variable::Variable(std::shared_ptr<VariableImpl> variable)
    : variable_(variable) {}

void Variable::setLB(double lb) {
  variable_->setLB(lb);
}

void Variable::setUB(double ub) {
  variable_->setUB(ub);
}

void Variable::setThreshold(double ub) {
  variable_->setThreshold(ub);
}

void Variable::setBounds(double lb, double ub) {
  setLB(lb);
  setUB(ub);
}

void Variable::setPartition(int partitionId) {
  variable_->setPartition(partitionId);
}

double Variable::getValue() const {
  return variable_->getValue();
}

double Variable::getReducedCost() const {
  return variable_->getReducedCost();
}

std::shared_ptr<const VariableImpl> Variable::get() const {
  return variable_;
}

} // namespace facebook::algopt::lp
