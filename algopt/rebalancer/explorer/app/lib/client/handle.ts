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

/**
 * @format
 */

/**
 * Handle resolution and per-sandbox metadata RPCs.
 */

import type {
  BundleExpirationResponse,
  Handle,
  HandleResponse,
  ProblemMetadataResponse,
  SandboxStatusResponse,
} from '../rebalancer-explorer-types';
import {getRpcClient, RebalancerExplorerBackendError} from './core';

/**
 * Resolve a Manifold ID to a sandbox handle.
 */
export async function getHandle(
  manifoldId: string,
  catToken?: string,
): Promise<HandleResponse> {
  try {
    const rpcClient = await getRpcClient(catToken);
    const response = await rpcClient.getHandle({
      request: {manifoldId, clientId: 'explorer_ui_nextjs'},
    });
    return response as HandleResponse;
  } catch (error) {
    throw new RebalancerExplorerBackendError('getHandle', error);
  }
}

export async function extendBundleExpiration(
  handle: Handle,
  secondsFromNow: number,
  catToken?: string,
): Promise<BundleExpirationResponse> {
  try {
    const rpcClient = await getRpcClient(catToken, {
      ip_addr: handle.host,
      port: handle.port,
    });
    const expiresAt = (await rpcClient.extendBundleExpiration({
      handle,
      secondsFromNow,
    })) as number;
    return {expiresAt};
  } catch (error) {
    throw new RebalancerExplorerBackendError('extendBundleExpiration', error);
  }
}

export async function getBundleExpiration(
  handle: Handle,
  catToken?: string,
): Promise<BundleExpirationResponse> {
  try {
    const rpcClient = await getRpcClient(catToken, {
      ip_addr: handle.host,
      port: handle.port,
    });
    const expiresAt = (await rpcClient.getBundleExpiration({handle})) as number;
    return {expiresAt};
  } catch (error) {
    throw new RebalancerExplorerBackendError('getBundleExpiration', error);
  }
}

/**
 * Check the loading status of a sandbox.
 *
 * Routes the RPC to the server identified in the handle (handle.host:handle.port)
 * so we always check the same server where the sandbox was loaded.
 */
export async function getSandboxStatus(
  handle: Handle,
  catToken?: string,
): Promise<SandboxStatusResponse> {
  try {
    const rpcClient = await getRpcClient(catToken, {
      ip_addr: handle.host,
      port: handle.port,
    });
    const response = await rpcClient.getSandboxStatus({handle});
    return response as SandboxStatusResponse;
  } catch (error) {
    throw new RebalancerExplorerBackendError('getSandboxStatus', error);
  }
}

/**
 * Retrieve problem metadata for a loaded sandbox.
 *
 * Routes the RPC to the server identified in the handle.
 */
export async function getProblemMetadata(
  handle: Handle,
  catToken?: string,
): Promise<ProblemMetadataResponse> {
  try {
    const rpcClient = await getRpcClient(catToken, {
      ip_addr: handle.host,
      port: handle.port,
    });
    const response = await rpcClient.getProblemMetadataV2({handle});
    return response as ProblemMetadataResponse;
  } catch (error) {
    throw new RebalancerExplorerBackendError('getProblemMetadata', error);
  }
}
