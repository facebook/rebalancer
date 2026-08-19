#!/usr/bin/env python3
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

import os
import tarfile
from datetime import timedelta
from io import BytesIO
from unittest.mock import MagicMock, Mock, mock_open

from algopt.rebalancer.common.manifold_utils import (
    asyncDownloadFromManifold,
    downloadFromManifold,
    unarchiveFromManifold,
    uploadToManifold,
)
from libfb.py.asyncio.mock import AsyncMagicMock, AsyncMock, patch
from libfb.py.asyncio.unittest import async_test
from libfb.py.testutil import BaseFacebookTestCase, data_provider


def get_mock_manifold_client() -> MagicMock:
    """
    Mock for ManifoldClient
    """
    m = MagicMock()
    m.__enter__.return_value = m
    m.get = AsyncMagicMock()
    m.return_value = m
    return m


def get_mock_manifold_download(
    file_name: str,
    # pyre-fixme[2]: Parameter must be annotated.
    data,
    # pyre-fixme[2]: Parameter must be annotated.
    file_obj,
    prefix: str,
    suffix: str,
) -> None:
    """
    A mock for downloadFromManifold that creates a temporary .tgz archive
    with a single file file_name with contents of test data.
    """
    with tarfile.open(fileobj=file_obj, mode="w:gz") as tar:
        tarinfo = tarfile.TarInfo(file_name)
        # pyre-fixme[35]: Target cannot be annotated.
        tarinfo.size: int = len(data)
        tar.addfile(tarinfo, BytesIO(data.encode()))


class ManifoldUtilsTest(BaseFacebookTestCase):
    def test_manifold_upload(self) -> None:
        sync_put_name = (
            "algopt.rebalancer.common.manifold_utils.ManifoldClient.sync_put"
        )

        def fake_sync_put(
            # pyre-fixme[2]: Parameter must be annotated.
            self,
            # pyre-fixme[2]: Parameter must be annotated.
            path,
            ttl: int = 0,
            parallelPrefixPath: str = "",
        ) -> None:
            pass

        with (
            patch(sync_put_name, side_effect=fake_sync_put) as sync_put_fn,
            patch("builtins.open", mock_open(read_data="data")) as mock_file,
        ):
            uploadToManifold("problem.json", "prefix", "uuid")

            mock_file.assert_called_with("problem.json", "rb")

            sync_put_fn.assert_called_once()

            call_args = sync_put_fn.call_args
            # call_args[0] is positional args and [1] is kwargs
            # path
            self.assertEqual(call_args[0][0], "flat/prefix_uuid")
            self.assertEqual(call_args[1]["ttl"], timedelta(days=180))
            self.assertEqual(
                call_args[1]["parallelPrefixPath"], "tree/upload_staging_prefix"
            )

    def test_manifold_download(self) -> None:
        sync_get_name = (
            "algopt.rebalancer.common.manifold_utils.ManifoldClient.sync_get"
        )

        # pyre-fixme[2]: Parameter must be annotated.
        def fake_sync_get(path, file_stream) -> None:
            pass

        with patch(sync_get_name, side_effect=fake_sync_get) as sync_get_fn:
            file_stream = Mock()

            downloadFromManifold(file_stream, "prefix", "uuid")

            sync_get_fn.assert_called_once()

            call_args = sync_get_fn.call_args
            # call_args[0] is positional args and [1] is kwargs
            # path
            self.assertEqual(call_args[0][0], "flat/prefix_uuid")
            # file stream
            self.assertEqual(call_args[0][1], file_stream)

    @async_test
    @patch(
        "algopt.rebalancer.common.manifold_utils.ManifoldClient",
        new_callable=get_mock_manifold_client,
    )
    # pyre-fixme[2]: Parameter must be annotated.
    async def test_async_manifold_download(self, mock_ManifoldClient) -> None:
        file_stream = Mock()

        await asyncDownloadFromManifold(file_stream, "prefix", "uuid")

        mock_ManifoldClient.get.assert_called_once()

        call_args = mock_ManifoldClient.get.call_args
        # call_args[0] is positional args and [1] is kwargs
        # path
        self.assertEqual(call_args[0][0], "flat/prefix_uuid")
        # file stream
        self.assertEqual(call_args[0][1], file_stream)

    #  "file_name":"testfile.json","data":"test data text" }))` to decorator factory
    #  `libfb.py.testutil.data_provider`.
    @data_provider(lambda: ({"file_name": "testfile.json", "data": "test data text"},))
    @async_test
    @patch(
        "algopt.rebalancer.common.manifold_utils.asyncDownloadFromManifold",
        new_callable=AsyncMock,
    )
    async def test_unarchive_manifold(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_asyncdownload,
        file_name: str,
        # pyre-fixme[2]: Parameter must be annotated.
        data,
    ) -> None:
        mock_asyncdownload.side_effect = lambda *args: get_mock_manifold_download(
            file_name, data, *args
        )

        async with unarchiveFromManifold(["prefix"], "uuid") as tmp_dir:
            mock_asyncdownload.assert_called_once()
            file_name = os.path.join(tmp_dir.name, file_name)
            self.assertTrue(os.path.isfile(file_name))
            with open(file_name, "rb") as f:
                self.assertEqual(f.read(), data.encode())
