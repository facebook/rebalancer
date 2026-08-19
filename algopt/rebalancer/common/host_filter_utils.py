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
from asyncio import gather

from ame.asset_tagging_service.thrift_clients import AssetTaggingService
from ame.asset_tagging_service.thrift_types import Tag
from ame.serf.clients.py.serf_thrift_python import Serf3ServiceClient
from dcconsole.dcconsole.thrift_clients import DCConsoleService
from dcconsole.dcconsole.thrift_types import (
    FilterProcessServersRequest as DCConsoleServiceFilterProcessServersRequest,
    FilterProcessServersResponse as DCConsoleServiceFilterProcessServersResponse,
)
from facebook.core_systems.cluster.thrift_types import ClusterRackType, ClusterState
from facebook.core_systems.device.thrift_types import (
    Device,
    DeviceStatus,
    EvaluationStatus,
    MaintenanceStatus,
)
from facebook.core_systems.queries.thrift_types import BinaryExpression, Operator, Query
from facebook.tupperware.twtf.twtf.thrift_clients import TwtfService
from facebook.tupperware.twtf.twtf.thrift_types import SearchAssetsRequest
from libfb.py.asyncio.gen import genv
from libfb.py.asyncio.smc import get_all_child_services
from rebalancer.fetching.migration_utils import (
    get_location_filters_for_upcoming_migrations_v2,
)
from servicerouter.python.async_client import get_sr_client as get_asr_client


async def query_els_for_tupperware_non_prod_hosts() -> set[str]:
    twshared_ex_hosts, tw_rc_hosts = await gather(
        query_els_for_twshared_experimental_hosts(), query_tupperware_rc_hosts()
    )
    return twshared_ex_hosts.union(tw_rc_hosts)


async def query_tupperware_rc_hosts() -> set[str]:
    try:
        tw_rc_services = list(
            await get_all_child_services("tupperware.rc.hosts", False)
        )
        tw_rc_hosts = {service.hostname for service in tw_rc_services}
        return tw_rc_hosts
    except Exception as ex:
        logging.exception(f"Failed to get all tupperware RC hosts: {ex}")
        return set()


async def query_els_for_twshared_experimental_hosts() -> set[str]:
    try:
        async with get_asr_client(TwtfService, "twtf.frontend") as client:
            request = SearchAssetsRequest()
            assets = await client.search_assets(request)
            return {asset.hostname for asset in assets}
    except Exception as ex:
        logging.exception(f"Failed to get all twshared experimental hosts: {ex}")
        return set()


async def query_ats_for_lemon_status() -> set[int]:
    try:
        # It's faster to fetch all lemons than checking each asset
        async with get_asr_client(
            AssetTaggingService, "ame.asset_tagging_service"
        ) as client:
            all_lemons = await client.getAssetIds(
                tag=Tag(tag_namespace="lemon", name="lemon_reported"),
                include_child_tags=False,
                include_expired_tags=False,
            )
            return set(all_lemons)
    except Exception as ex:
        logging.exception(f"Failed to get lemon assets: {ex}")
        return set()


async def query_serf_for_asset_ids(query: Query) -> set[Device]:
    try:
        async with Serf3ServiceClient() as serf:
            devices = await serf.getDevices(
                query=query,
                columns=["asset_id", "hostnameScheme_obj.name"],
            )
            return set(devices)
    except Exception as ex:
        logging.exception(f"Failed to get serf devices: {ex}")
        return set()


async def query_serf_for_datacenter_names(query: Query) -> list[str]:
    try:
        async with Serf3ServiceClient() as serf_client:
            datacenters = await serf_client.getDatacenters(
                query=query,
                columns=["name"],
            )
            return [datacenter.name for datacenter in datacenters]
    except Exception as ex:
        logging.exception(f"Failed to query serf for datacenters: {ex}")
        return []


async def gen_decomed_assets(
    region: str, logical_server_types: list[int]
) -> set[Device]:
    region = region.lower()
    query = Query(
        binExprs=[
            BinaryExpression(
                name="name", comparison=Operator.LIKE, values=[f"{region}%"]
            )
        ]
    )
    datacenters = await query_serf_for_datacenter_names(query)

    # If no datacenters are queried from serf, we return empty set
    if not datacenters:
        return set()

    decom_location_filters = await get_location_filters_for_upcoming_migrations_v2(
        datacenters
    )
    lsts_to_fetch = [str(lst) for lst in logical_server_types]

    queries = []
    for decom_location in decom_location_filters:
        expressions = [
            BinaryExpression(
                name="logical_server_type",
                comparison=Operator.IN,
                values=lsts_to_fetch,
            ),
        ]
        if decom_location.get("cluster"):
            expressions.append(
                BinaryExpression(
                    name="cluster",
                    comparison=Operator.EQUAL,
                    #  `Optional[Sequence[str]]` but got `list[Union[None, set[Any],
                    #  str]]`.
                    values=[decom_location.get("cluster")],
                ),
            )
        if decom_location.get("suite"):
            expressions.append(
                BinaryExpression(
                    name="suite",
                    comparison=Operator.EQUAL,
                    #  `Optional[Sequence[str]]` but got `list[Union[None, set[Any],
                    #  str]]`.
                    values=[decom_location.get("suite")],
                ),
            )
        if decom_location.get("datacenter"):
            expressions.append(
                BinaryExpression(
                    name="datacenter",
                    comparison=Operator.EQUAL,
                    #  `Optional[Sequence[str]]` but got `list[Union[None, set[Any],
                    #  str]]`.
                    values=[decom_location.get("datacenter")],
                ),
            )
        if decom_location.get("row"):
            expressions.append(
                BinaryExpression(
                    name="row",
                    comparison=Operator.IN,
                    #  `Optional[Sequence[str]]` but got `Union[None, set[Any], str]`.
                    values=decom_location.get("row"),
                )
            )
        if decom_location.get("rack_position"):
            expressions.append(
                BinaryExpression(
                    name="rack",
                    comparison=Operator.IN,
                    #  `Optional[Sequence[str]]` but got `Union[None, set[Any], str]`.
                    values=decom_location.get("rack_position"),
                ),
            )

        queries.append(Query(binExprs=expressions))

    decommed_assets_sets = await genv(
        [query_serf_for_asset_ids(decommed_query) for decommed_query in queries]
    )
    decommed_assets = set()
    for asset_set in decommed_assets_sets:
        for asset in asset_set:
            decommed_assets.add(asset)

    return decommed_assets


def get_serf_filters_for_movable_servers(
    filters_to_ignore: set[str] | None = None,
) -> list[BinaryExpression]:
    # apply serf filters to remove devices that are not movable
    # this should be in sync with Rebalancer's serf filters
    # in fbcode/algopt/rebalancer/common/spec_utils.py
    if not filters_to_ignore:
        filters_to_ignore = set()
    movable_server_filters = [
        BinaryExpression(
            name="status",
            comparison=Operator.IN,
            values=[
                str(DeviceStatus.IN_USE.value),
                str(DeviceStatus.INSTALLED.value),
                str(DeviceStatus.STANDBY.value),
                str(DeviceStatus.UNASSIGNED.value),
            ],
        ),
        BinaryExpression(
            name="maintenance_status",
            comparison=Operator.IN,
            values=[
                str(MaintenanceStatus.NONE.value),
            ],
        ),
        BinaryExpression(
            name="cluster_obj.state",
            comparison=Operator.EQUAL,
            values=[str(ClusterState.CLUSTER_IN_USE.value)],
        ),
        BinaryExpression(
            name="evaluation_status",
            comparison=Operator.IN,
            values=[
                str(EvaluationStatus.EVAL_MASS_PRODUCTION.value),
                str(EvaluationStatus.EVAL_MP_TESTING.value),
                str(EvaluationStatus.EVAL_MP_PILOT.value),
                str(EvaluationStatus.EVAL_IT.value),
                str(EvaluationStatus.EVAL_EDGE.value),
            ],
        ),
        BinaryExpression(
            name="clusterRack_obj.type",
            comparison=Operator.NOT_IN,
            values=[
                str(ClusterRackType.RACK_DEV.value),
            ],
        ),
        BinaryExpression(
            name="cluster",
            comparison=Operator.NOT_IN,
            values=["99"],
        ),
        BinaryExpression(
            name="clusterRack_obj.failure_domain",
            comparison=Operator.IS_NOT_NULL,
            values=[],
        ),
    ]

    return list(
        filter(
            lambda server_filter: server_filter.name not in filters_to_ignore,
            movable_server_filters,
        )
    )


async def get_turn_down_servers_asset_ids(asset_ids: set[int]) -> set[int]:
    if len(asset_ids) == 0:
        return set()

    async with get_asr_client(DCConsoleService) as client:
        res: DCConsoleServiceFilterProcessServersResponse = (
            await client.filterActiveTurnDownServers(
                DCConsoleServiceFilterProcessServersRequest(asset_ids=list(asset_ids))
            )
        )
        return set(res.asset_ids)
