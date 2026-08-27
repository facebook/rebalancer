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
from typing import Any, cast, Union

from algopt.rebalancer.common import utils
from algopt.rebalancer.common.host_data import HostData
from algopt.rebalancer.common.job_data import JobData
from algopt.rebalancer.common.rack_data import RackData
from algopt.rebalancer.common.scope import ScopeT, SerfScope, SubRegionFDScope
from algopt.rebalancer.common.task_data import TaskData


DataMap = dict[str, Union[HostData, RackData, utils.Holder]]


class StateHolder(utils.Holder):
    hosts_data: dict[str, HostData]
    racks_data: dict[str, RackData]
    tasks_data: dict[str, TaskData]
    jobs_data: dict[str, JobData]
    serf_scopes: dict[str, SerfScope]
    config_map: dict[str, Any]
    folder: str = ""
    sub_region_fd: dict[str, SubRegionFDScope]
    run_uuid: str = ""
    power_dimensions: list[str]
    network_dimensions: list[str]
    groups_with_shortage: set[str]

    def __init__(self, **kwargs: Any) -> None:
        self.config_map = {}
        super().__init__(**kwargs)

    def scopes(self) -> dict[str, ScopeT]:
        """Combines all scopes we have in one dict."""
        scopes = {}
        if hasattr(self, "serf_scopes"):
            scopes.update(self.serf_scopes)
        if hasattr(self, "sub_region_fd"):
            # pyrefly: ignore [no-matching-overload]
            scopes.update(self.sub_region_fd)
        # pyrefly: ignore [bad-return]
        return scopes

    def get_scope_by_name(self, scope_name: str) -> ScopeT | None:
        """
        Search all scopes in the state for the `scope_name` and
        returns it or None if cannot find it.
        """
        if hasattr(self, "serf_scopes"):
            if scope_name in self.serf_scopes:
                return self.serf_scopes[scope_name]
        if hasattr(self, "sub_region_fd"):
            if scope_name in self.sub_region_fd:
                return self.sub_region_fd[scope_name]
        return None

    def __getitem__(self, key: Any) -> Any:
        logging.debug("accessing " + str(key))
        return getattr(self, key)

    def __contains__(self, key: Any) -> bool:
        logging.debug("checking " + str(key))
        return hasattr(self, key)

    def __setitem__(self, key: Any, value: Any) -> None:
        logging.debug("setting " + str(key))
        utils.checkState(
            not hasattr(self, key), "%s already defined in StateHolder", key
        )
        return setattr(self, key, value)

    def getObjectsData(self) -> DataMap:
        # This is a lie to satisfy the type checker.
        return cast(DataMap, getattr(self, self.config_map["object_name"] + "s_data"))

    def getContainersData(self) -> DataMap:
        return cast(
            DataMap, getattr(self, self.config_map["container_name"] + "s_data")
        )

    def getObjectName(self) -> str:
        return cast(str, self.config_map["object_name"])

    def getContainerName(self) -> str:
        return cast(str, self.config_map["container_name"])

    def createJsonFile(self, name: str, content: Any) -> str:
        return utils.createJsonFile(self.folder, name, content)

    def getAdditionalObjectsData(self) -> DataMap:
        name = "additional_{}s_data".format(self.config_map["object_name"])
        if hasattr(self, name):
            return cast(DataMap, getattr(self, name))
        return {}
