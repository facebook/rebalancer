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

import logging
import tarfile
import tempfile
from contextlib import asynccontextmanager
from datetime import timedelta
from typing import IO

from manifold.clients.python.manifold_client import ManifoldClient


_BUCKET = "rebalancer"
_PATH_FMT = "flat/{}_{}"


# pyre-fixme[2]: Parameter must be annotated.
def uploadToManifold(filepath, prefix: str, run_uuid) -> None:
    with ManifoldClient.get_client(_BUCKET) as manifold_client:
        with open(filepath, "rb") as file_stream:
            path = _PATH_FMT.format(prefix, run_uuid)

            logging.info(
                "Uploading %s for run UUID %s in bucket %s at path %s",
                prefix,
                run_uuid,
                _BUCKET,
                path,
            )
            manifold_client.sync_put(
                path,
                file_stream,
                parallelPrefixPath=f"tree/upload_staging_{prefix}",
                # Expire after 180 days
                ttl=timedelta(days=180),
            )


# pyre-fixme[2]: Parameter must be annotated.
def downloadFromManifold(file_stream, prefix: str, run_uuid) -> None:
    # pyre-fixme[45]: Cannot instantiate abstract class `ManifoldClient`.
    with ManifoldClient(_BUCKET) as manifold:
        path = _PATH_FMT.format(prefix, run_uuid)
        manifold.sync_get(path, file_stream)


async def asyncDownloadFromManifold(
    file_stream: IO[bytes],
    prefix: str,
    # pyre-fixme[2]: Parameter must be annotated.
    run_uuid,
) -> None:
    # pyre-fixme[45]: Cannot instantiate abstract class `ManifoldClient`.
    with ManifoldClient(_BUCKET) as manifold:
        path = _PATH_FMT.format(prefix, run_uuid)
        await manifold.get(path, file_stream)


@asynccontextmanager
# pyre-fixme[3]: Return type must be annotated.
# pyre-fixme[2]: Parameter must be annotated.
async def unarchiveFromManifold(prefixes: list[str], run_uuid):
    """
    Downloads and extracts multiple tgz files from manifold given a list of prefix.
    For each prefix it downloads from flat/{prefix}_{run_uuid} and extracts it to
    a temporary directory that is returned as a context.
    Note: All the files should have unique name as they'll all be extracted to the
    same directory
    """
    try:
        temp_dir: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        for prefix in prefixes:
            with tempfile.NamedTemporaryFile(suffix=".tgz") as temp_file:
                # NamedTemporaryFile returns a wrapper around the file object,
                # so we need to give the underlying file

                # raises StorageException if path not found
                await asyncDownloadFromManifold(
                    temp_file.file,
                    prefix,
                    run_uuid,
                )
                temp_file.flush()

                with tarfile.open(temp_file.name, mode="r:gz") as tgz:
                    tgz.extractall(path=temp_dir.name, filter="data")

        yield temp_dir
    finally:
        temp_dir.cleanup()
