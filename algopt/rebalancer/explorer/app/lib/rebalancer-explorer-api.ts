/**
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

import type {
  Assignment,
  BundleExpirationResponse,
  ConstraintSpecResponse,
  DataResponse,
  EvaluateResponse,
  GoalSpecResponse,
  Handle,
  HandleResponse,
  LocalSearchProfilesResponse,
  MetricDistributionRequest,
  MetricDistributionResponse,
  MovesBetweenAssignmentsResponse,
  MoveSetsRequest,
  MoveSetsResponse,
  ProblemMetadataResponse,
  Query,
  Result,
  SandboxStatusResponse,
  TreeNodeRequest,
  TreeNodeResponse,
  TypeaheadResponse,
} from './rebalancer-explorer-types';

export const DEFAULT_EXPIRATION_DAYS = 90;
export const SECONDS_PER_DAY = 86_400;
const MAX_MANIFOLD_EXPIRATION_TIMESTAMP_SECONDS = 2_147_483_647;
const MAX_EXPIRATION_EXTENSION_DAYS = Math.floor(
  MAX_MANIFOLD_EXPIRATION_TIMESTAMP_SECONDS / SECONDS_PER_DAY,
);

export function isValidExpirationDays(extensionDays: number): boolean {
  return (
    Number.isSafeInteger(extensionDays) &&
    extensionDays >= 0 &&
    extensionDays <= MAX_EXPIRATION_EXTENSION_DAYS
  );
}

export function expirationDaysToSeconds(extensionDays: number): number {
  return extensionDays * SECONDS_PER_DAY;
}

export function getExpirationExtensionSecondsFromNow(
  currentExpiration: number,
  extensionDays: number,
  nowMs: number = Date.now(),
): number | null {
  if (
    !Number.isSafeInteger(currentExpiration) ||
    currentExpiration < 0 ||
    !isValidExpirationDays(extensionDays)
  ) {
    return null;
  }
  if (currentExpiration === 0 || extensionDays === 0) {
    return 0;
  }

  const extendedExpiration =
    currentExpiration + expirationDaysToSeconds(extensionDays);
  const secondsFromNow = extendedExpiration - Math.floor(nowMs / 1000);
  if (
    !Number.isSafeInteger(extendedExpiration) ||
    extendedExpiration > MAX_MANIFOLD_EXPIRATION_TIMESTAMP_SECONDS ||
    secondsFromNow <= 0
  ) {
    return null;
  }
  return secondsFromNow;
}

async function parseBundleExpirationResponse(
  response: Response,
): Promise<BundleExpirationResponse> {
  const body: unknown = await response.json();
  if (
    typeof body !== 'object' ||
    body == null ||
    !('expiresAt' in body) ||
    typeof body.expiresAt !== 'number' ||
    !Number.isFinite(body.expiresAt) ||
    !Number.isInteger(body.expiresAt) ||
    body.expiresAt < 0
  ) {
    throw new Error('Invalid expiration response');
  }

  return {expiresAt: body.expiresAt};
}

export async function fetchHandle(manifoldId: string): Promise<HandleResponse> {
  const response = await fetch('/api/rebalancer/handle', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({manifoldId}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch handle');
  }

  return response.json();
}

export async function fetchSandboxStatus(
  handle: Handle,
): Promise<SandboxStatusResponse> {
  const response = await fetch('/api/rebalancer/sandbox-status', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({handle}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch sandbox status');
  }

  return response.json();
}

export async function fetchProblemMetadata(
  handle: Handle,
): Promise<ProblemMetadataResponse> {
  const response = await fetch('/api/rebalancer/problem-metadata', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({handle}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch problem metadata');
  }

  return response.json();
}

export async function extendBundleExpiration(
  handle: Handle,
  extensionDays: number,
): Promise<BundleExpirationResponse> {
  const response = await fetch('/api/rebalancer/bundle-expiration', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-Requested-With': 'fetch',
    },
    credentials: 'include',
    body: JSON.stringify({handle, extensionDays}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to extend expiration');
  }

  return parseBundleExpirationResponse(response);
}

export async function fetchBundleExpiration(
  handle: Handle,
): Promise<BundleExpirationResponse> {
  const params = new URLSearchParams({
    manifoldId: handle.manifoldId,
    host: handle.host,
    port: handle.port.toString(),
    taskId: handle.taskId.toString(),
  });
  const response = await fetch(`/api/rebalancer/bundle-expiration?${params}`, {
    credentials: 'include',
    cache: 'no-store',
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch expiration');
  }

  return parseBundleExpirationResponse(response);
}

export async function fetchEvaluation(
  handle: Handle,
  assignment: Assignment,
): Promise<EvaluateResponse> {
  const response = await fetch('/api/rebalancer/evaluate', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, assignment}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch evaluation');
  }

  return response.json();
}

export async function fetchEntityData(
  handle: Handle,
  query: Query,
): Promise<DataResponse> {
  const response = await fetch('/api/rebalancer/entity-data', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, query}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch entity data');
  }

  return response.json();
}

export async function fetchMetricDistribution(
  handle: Handle,
  requests: MetricDistributionRequest[],
): Promise<MetricDistributionResponse[]> {
  const response = await fetch('/api/rebalancer/metric-distribution', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, requests}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch metric distribution');
  }

  const body: {responses: MetricDistributionResponse[]} = await response.json();
  return body.responses;
}

export async function fetchTypeahead(
  handle: Handle,
  entity: string,
  query: string,
  limit: number,
): Promise<TypeaheadResponse> {
  const response = await fetch('/api/rebalancer/typeahead', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, entity, query, limit}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch typeahead results');
  }

  return response.json();
}

export async function fetchGoalSpec(
  handle: Handle,
  name: string,
): Promise<GoalSpecResponse> {
  const response = await fetch('/api/rebalancer/goal-spec', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, name}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch goal spec');
  }

  return response.json();
}

export async function fetchConstraintSpec(
  handle: Handle,
  name: string,
): Promise<ConstraintSpecResponse> {
  const response = await fetch('/api/rebalancer/constraint-spec', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, name}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch constraint spec');
  }

  return response.json();
}

export async function fetchTreeNode(
  handle: Handle,
  request: TreeNodeRequest,
): Promise<TreeNodeResponse> {
  const response = await fetch('/api/rebalancer/tree-node', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, request}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch tree node');
  }

  return response.json();
}

export async function fetchMovesBetween(
  handle: Handle,
  source: Assignment,
  destination: Assignment,
): Promise<MovesBetweenAssignmentsResponse> {
  const response = await fetch('/api/rebalancer/moves-between', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, source, destination}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch moves between assignments');
  }

  return response.json();
}

export async function fetchMoveSets(
  handle: Handle,
  request: MoveSetsRequest,
): Promise<MoveSetsResponse> {
  const response = await fetch('/api/rebalancer/move-sets', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, request}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch move sets');
  }

  return response.json();
}

export async function fetchLocalSearchProfiles(
  handle: Handle,
): Promise<LocalSearchProfilesResponse> {
  const response = await fetch('/api/rebalancer/local-search-profiles', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch local search profiles');
  }

  return response.json();
}

export async function fetchMetricCollection(
  handle: Handle,
  query: Query,
  assignmentA: Assignment,
  assignmentB: Assignment,
): Promise<Result> {
  const response = await fetch('/api/rebalancer/evaluate-metric-collection', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    credentials: 'include',
    body: JSON.stringify({handle, query, assignmentA, assignmentB}),
  });

  if (!response.ok) {
    const body = await response.json();
    throw new Error(body.error ?? 'Failed to fetch metric collection');
  }

  return response.json();
}
