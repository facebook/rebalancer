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

import {afterEach, describe, expect, it, vi} from 'vitest';

import {
  extendBundleExpiration,
  expirationDaysToSeconds,
  fetchBundleExpiration,
  getExpirationExtensionSecondsFromNow,
  isValidExpirationDays,
} from '@/lib/rebalancer-explorer-api';
import type {Handle} from '@/lib/rebalancer-explorer-types';

const handle: Handle = {
  manifoldId: 'test-bundle',
  host: 'localhost',
  port: 8080,
  taskId: 1,
};

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('fetchBundleExpiration', () => {
  it('returns a valid expiration', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(JSON.stringify({expiresAt: 1_800_000_000}), {
          status: 200,
        }),
      ),
    );

    await expect(fetchBundleExpiration(handle)).resolves.toEqual({
      expiresAt: 1_800_000_000,
    });
  });

  it.each([
    {},
    {expiresAt: null},
    {expiresAt: 'never'},
    {expiresAt: -1},
    {expiresAt: 1.5},
  ])('rejects a malformed successful response: %o', async body => {
    vi.stubGlobal(
      'fetch',
      vi
        .fn()
        .mockResolvedValue(new Response(JSON.stringify(body), {status: 200})),
    );

    await expect(fetchBundleExpiration(handle)).rejects.toThrow(
      'Invalid expiration response',
    );
  });
});

describe('expiration duration validation', () => {
  it('converts days to seconds', () => {
    expect(expirationDaysToSeconds(90)).toBe(7_776_000);
  });

  it('accepts permanent retention and positive whole days', () => {
    expect(isValidExpirationDays(0)).toBe(true);
    expect(isValidExpirationDays(90)).toBe(true);
  });

  it('rejects invalid and unrepresentable day counts', () => {
    expect(isValidExpirationDays(-1)).toBe(false);
    expect(isValidExpirationDays(1.5)).toBe(false);
    expect(isValidExpirationDays(25_000)).toBe(false);
  });

  it('adds extension days to the current expiration', () => {
    expect(
      getExpirationExtensionSecondsFromNow(1_800_000_000, 1, 1_700_000_000_000),
    ).toBe(100_086_400);
  });

  it('preserves permanent retention', () => {
    expect(getExpirationExtensionSecondsFromNow(0, 90, 1_700_000_000_000)).toBe(
      0,
    );
    expect(
      getExpirationExtensionSecondsFromNow(1_800_000_000, 0, 1_700_000_000_000),
    ).toBe(0);
  });

  it('rejects an extension beyond the representable expiration', () => {
    expect(
      getExpirationExtensionSecondsFromNow(
        2_147_000_000,
        90,
        1_700_000_000_000,
      ),
    ).toBeNull();
  });
});

describe('extendBundleExpiration', () => {
  it('sends the requested number of days', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({expiresAt: 1_800_000_000}), {
        status: 200,
      }),
    );
    vi.stubGlobal('fetch', fetchMock);

    await extendBundleExpiration(handle, 45);

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/rebalancer/bundle-expiration',
      expect.objectContaining({
        body: JSON.stringify({handle, extensionDays: 45}),
      }),
    );
  });
});
