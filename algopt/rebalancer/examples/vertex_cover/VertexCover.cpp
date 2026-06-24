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

#include "algopt/rebalancer/examples/common/BundleOutput.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include "fmt/core.h"
#include <folly/container/irange.h>
#include <folly/init/Init.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace facebook::rebalancer::interface;

DEFINE_string(
    input_file,
    "",
    "Path to the input file containing edges (one edge per line: v1 v2)");

using Vertex = int;
using Edge = std::pair<Vertex, Vertex>;

std::tuple<folly::F14FastSet<Vertex>, std::vector<Edge>> readGraphFromFile(
    const std::string& filename) {
  folly::F14FastSet<Vertex> vertices;
  std::vector<Edge> edges;

  std::ifstream infile(filename);
  if (!infile.is_open()) {
    XLOG(ERR) << "Failed to open file: " << filename;
    return {vertices, edges};
  }

  std::string line;
  while (std::getline(infile, line)) {
    // Skip empty lines and lines starting with #
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // Parse the line to extract vertices
    std::istringstream iss(line);
    Vertex src, dst;
    if (iss >> src >> dst) {
      edges.emplace_back(src, dst);
      vertices.insert(src);
      vertices.insert(dst);
    }
  }
  infile.close();
  return {vertices, edges};
}

std::string getVertexName(Vertex v) {
  return fmt::format("v_{}", v);
}

std::string getEdgeName(Vertex v1, Vertex v2) {
  return fmt::format("{}-{}", v1, v2);
}

std::unique_ptr<ProblemSolver> makeRebalancerModelForVertexCover(
    const folly::F14FastSet<Vertex>& vertices,
    const std::vector<Edge>& edges) {
  auto solver =
      ProblemSolverFactory::makeProblemSolver("rebalancer", "coding_contest");

  solver->setObjectName("vertex");
  solver->setContainerName("bucket");

  // Create two buckets: "selected" and "not_selected"
  // Initially place all vertices in "not_selected" bucket
  std::map<std::string, std::vector<std::string>> initialAssignment;
  initialAssignment["selected"] = {};
  for (const auto v : vertices) {
    initialAssignment["not_selected"].emplace_back(getVertexName(v));
  }
  solver->setAssignment(initialAssignment);

  // Create an edge partition: one group per edge containing its two vertices
  // Note that partitions in Rebalancer are not necessarily disjoint
  folly::F14FastMap<std::string, std::vector<std::string>> edgeToItsVertices;
  for (auto edge : edges) {
    const auto [v1, v2] = edge;
    const auto edgeName = fmt::format("{}-{}", v1, v2);
    edgeToItsVertices[edgeName].emplace_back(getVertexName(v1));
    edgeToItsVertices[edgeName].emplace_back(getVertexName(v2));
  }
  solver->addPartition("edge_partition", std::move(edgeToItsVertices));

  // Add constraints: for each edge, at least one vertex must be in "selected"
  GroupCountSpec groupCountSpec;
  groupCountSpec.name() = "Cover all edges";
  groupCountSpec.scope() = "bucket";
  groupCountSpec.partitionName() = "edge_partition";
  groupCountSpec.filter()->itemsWhitelist() = {"selected"};
  groupCountSpec.bound() = GroupCountSpecBound::MIN;
  groupCountSpec.limit()->type() = LimitType::ABSOLUTE;
  // at least one vertex must be in "selected"
  groupCountSpec.limit()->globalLimit() = 1;
  solver->addConstraint(std::move(groupCountSpec));

  // objective that minimizes the number of vertices in "selected"
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "bucket";
  capacitySpec.dimension() = "vertex_count";
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 0;
  capacitySpec.filter()->itemsWhitelist() = {"selected"};
  solver->addGoal(std::move(capacitySpec));

  return solver;
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  if (FLAGS_input_file.empty()) {
    XLOG(ERR) << "Please provide an input file using --input_file flag";
    return 1;
  }
  auto [vertices, edges] = readGraphFromFile(FLAGS_input_file);

  XLOG(INFO) << fmt::format(
      "Read {} vertices and {} edges from {}",
      vertices.size(),
      edges.size(),
      FLAGS_input_file);

  auto solver = makeRebalancerModelForVertexCover(vertices, edges);

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.solverPackage() = OptimalSolverPackage::HIGHS;
  optimalSolverSpec.solveTime() = 30;
  solver->addSolver(optimalSolverSpec);

  auto solution = solver->solve();
  facebook::rebalancer::examples::maybeSaveBundle(*solver);

  const std::vector<std::string> header = {"Vertex", "Included in cover?"};
  std::vector<std::vector<std::string>> rows;
  for (const auto& [vertexName, status] : *solution.assignment()) {
    rows.push_back({vertexName, status == "selected" ? "✅" : "❌"});
  }
  XLOG(INFO) << facebook::algopt::utils::formatAsPrettyTable(
      rows,
      header,
      /*cellWidth=*/25,
      /*sortByCol=*/1);
  return 0;
}
