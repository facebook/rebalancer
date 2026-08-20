# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

"""Shared server-selection logic for the RebalancerExplorer SMC tier.

RebalancerExplorer is a multi-tenant service: each server loads sandboxes on
demand and pins them to itself. When a client resolves a handle for a run it
should route the ``getHandle`` call to a good server rather than relying on
plain (random) ServiceRouter routing. This module centralizes that decision so
every client (CABS, GSP analyzer, ...) shares one implementation instead of
re-deriving it.

The strategy mirrors the Hack ``RebalancerExplorerClientFactory`` and the Nest
``server-stickiness`` logic:

1. Sticky / already-loaded: prefer a server that already has the sandbox loaded
   (a cache hit), then one where it is loading or errored.
2. Best server: otherwise pick the least-loaded server -- fewest actively
   loading sandboxes, tie-broken by the most free memory -- via
   ``getServerStatus``.
3. Fallback: if server discovery fails or no server responds, return ``None``
   so the caller falls back to default ServiceRouter routing.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from typing import Final, Sequence

from rebalancer.explorer.explorer.thrift_clients import RebalancerExplorerService
from rebalancer.explorer.explorer.thrift_types import (
    Handle,
    SandboxStatus,
    ServerStatus,
)
from servicerouter.python.async_client import get_sr_client
from servicerouter.python.client_params import ClientParams
from servicerouter.srproxy_asyncio import get_hosts as sr_get_hosts

logger: logging.Logger = logging.getLogger(__name__)

SMC_TIER: Final[str] = "rebalancer_explorer"
# Short timeout for status probes: an unresponsive server should be skipped, not
# block handle resolution.
STATUS_TIMEOUT_MS: Final[int] = 1000
DEFAULT_CLIENT_ID: Final[str] = "rebalancer_explorer_client"

# Priorities returned by the already-loaded probe (higher is better).
_PRIORITY_LOADED: Final[int] = 2
_PRIORITY_EXISTS: Final[int] = 1
_PRIORITY_NONE: Final[int] = 0


@dataclass(frozen=True)
class ServerAddr:
    """A single RebalancerExplorer server address."""

    host: str
    port: int


def select_best_server(
    statuses: Sequence[ServerStatus | None],
    addrs: Sequence[ServerAddr],
) -> ServerAddr | None:
    """Pick the least-loaded server.

    Chooses the server with the fewest actively loading sandboxes, breaking ties
    by the most free memory. ``None`` entries (unreachable servers) are skipped.
    Returns ``None`` if no server reported its status.

    Pure function over already-collected statuses so it can be unit-tested
    without any RPC.
    """
    best: ServerAddr | None = None
    best_loading = 0
    best_memory = 0
    for status, addr in zip(statuses, addrs):
        if status is None:
            continue
        if (
            best is None
            or status.loadingSandboxCount < best_loading
            or (
                status.loadingSandboxCount == best_loading
                and status.freeMemoryBytes > best_memory
            )
        ):
            best = addr
            best_loading = status.loadingSandboxCount
            best_memory = status.freeMemoryBytes
    return best


def _pinned_params(addr: ServerAddr, timeout_ms: int, client_id: str) -> ClientParams:
    params = ClientParams()
    params.setProcessingTimeoutMs(timeout_ms)
    params.setClientId(client_id)
    params.setSingleHost(ipAddr=addr.host, port=addr.port)
    return params


async def _probe_status(
    addr: ServerAddr, tier: str, timeout_ms: int, client_id: str
) -> ServerStatus | None:
    """Query one server's load, returning ``None`` if it is unreachable."""
    try:
        async with get_sr_client(
            RebalancerExplorerService,
            tier,
            params=_pinned_params(addr, timeout_ms, client_id),
        ) as client:
            return await client.getServerStatus()
    except Exception as e:
        logger.debug("getServerStatus failed for %s:%s: %s", addr.host, addr.port, e)
        return None


async def _probe_priority(
    addr: ServerAddr,
    tier: str,
    manifold_id: str,
    timeout_ms: int,
    client_id: str,
) -> int:
    """Rank a server for a manifoldId by its sandbox status.

    ``2`` = already loaded (best), ``1`` = exists but loading/errored,
    ``0`` = not loaded or server unreachable.
    """
    try:
        async with get_sr_client(
            RebalancerExplorerService,
            tier,
            params=_pinned_params(addr, timeout_ms, client_id),
        ) as client:
            response = await client.getSandboxStatus(Handle(manifoldId=manifold_id))
    except Exception as e:
        logger.debug("getSandboxStatus failed for %s:%s: %s", addr.host, addr.port, e)
        return _PRIORITY_NONE
    if response.status == SandboxStatus.NOT_LOADED:
        return _PRIORITY_NONE
    if response.status == SandboxStatus.LOADED:
        return _PRIORITY_LOADED
    return _PRIORITY_EXISTS


async def _discover_servers(tier: str) -> list[ServerAddr]:
    """Enumerate the hosts behind the SMC tier."""
    try:
        # options/overrides must be non-None: srproxy dereferences them.
        hosts = await sr_get_hosts(
            tier=tier,
            options={
                "svc_prefer_localities": "global",
                "svc_select_count": "0",
                "svc_try_connect": "true",
            },
            overrides={},
        )
    except Exception as e:
        logger.warning("Failed to discover hosts for tier %s: %s", tier, e)
        return []
    return [ServerAddr(host=h.ip, port=h.port) for h in hosts]


async def _find_already_loaded(
    addrs: Sequence[ServerAddr],
    tier: str,
    manifold_id: str,
    timeout_ms: int,
    client_id: str,
) -> ServerAddr | None:
    """Return the highest-priority server that already has the sandbox, if any."""
    priorities = await asyncio.gather(
        *(_probe_priority(a, tier, manifold_id, timeout_ms, client_id) for a in addrs)
    )
    best: ServerAddr | None = None
    best_priority = _PRIORITY_NONE
    for priority, addr in zip(priorities, addrs):
        if priority > best_priority:
            best_priority = priority
            best = addr
    return best


async def resolve_target_server(
    tier: str = SMC_TIER,
    manifold_id: str | None = None,
    *,
    timeout_ms: int = STATUS_TIMEOUT_MS,
    client_id: str = DEFAULT_CLIENT_ID,
) -> ServerAddr | None:
    """Resolve the best server to send a ``getHandle`` call to.

    Mirrors ``RebalancerExplorerClientFactory::genFromManifoldId``:

    1. If ``manifold_id`` is given, prefer a server that already has the sandbox
       loaded (cache hit).
    2. Otherwise pick the least-loaded server (fewest loading sandboxes, then
       most free memory).
    3. Return ``None`` if discovery fails or no server responds, signalling the
       caller to fall back to default ServiceRouter routing.
    """
    addrs = await _discover_servers(tier)
    if not addrs:
        return None

    if manifold_id is not None:
        loaded = await _find_already_loaded(
            addrs, tier, manifold_id, timeout_ms, client_id
        )
        if loaded is not None:
            return loaded

    statuses = await asyncio.gather(
        *(_probe_status(a, tier, timeout_ms, client_id) for a in addrs)
    )
    return select_best_server(statuses, addrs)
