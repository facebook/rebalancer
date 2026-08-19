# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# pyre-strict
import asyncio
from tempfile import TemporaryDirectory

from algopt.rebalancer.common.local_cache import _rebalancer_local_cache
from libfb.py import testutil


class TestUtils(testutil.BaseFacebookTestCase):
    def test_local_cache(self) -> None:
        with TemporaryDirectory() as tmp_dir:
            # pyre-fixme[16]: `TestUtils` has no attribute `x`.
            self.x = 0

            @_rebalancer_local_cache(cache_path=tmp_dir)
            def cached_inc(arg: str) -> int:
                self.x += 1
                return self.x

            res = cached_inc("a")
            self.assertEqual(1, res)
            self.assertEqual(1, self.x)

            res = cached_inc("a")
            self.assertEqual(1, res)
            self.assertEqual(1, self.x)

            res = cached_inc("b")
            self.assertEqual(2, res)
            self.assertEqual(2, self.x)

            res = cached_inc("c")
            self.assertEqual(3, res)
            self.assertEqual(3, self.x)

            res = cached_inc("a")
            self.assertEqual(1, res)
            self.assertEqual(3, self.x)

            res = cached_inc("b")
            self.assertEqual(2, res)
            self.assertEqual(3, self.x)

            res = cached_inc("c")
            self.assertEqual(3, res)
            self.assertEqual(3, self.x)

    async def async_local_cache(self) -> None:
        with TemporaryDirectory() as tmp_dir:
            self.x = 0

            @_rebalancer_local_cache(cache_path=tmp_dir)
            async def async_cached_inc(arg: str) -> int:
                self.x += 1
                return self.x

            res = await async_cached_inc("a")
            self.assertEqual(1, res)
            self.assertEqual(1, self.x)

            res = await async_cached_inc("a")
            self.assertEqual(1, res)
            self.assertEqual(1, self.x)

            res = await async_cached_inc("b")
            self.assertEqual(2, res)
            self.assertEqual(2, self.x)

            res = await async_cached_inc("c")
            self.assertEqual(3, res)
            self.assertEqual(3, self.x)

            res = await async_cached_inc("a")
            self.assertEqual(1, res)
            self.assertEqual(3, self.x)

            res = await async_cached_inc("b")
            self.assertEqual(2, res)
            self.assertEqual(3, self.x)

            res = await async_cached_inc("c")
            self.assertEqual(3, res)
            self.assertEqual(3, self.x)

    def test_async_local_cache(self) -> None:
        asyncio.run(self.async_local_cache())
