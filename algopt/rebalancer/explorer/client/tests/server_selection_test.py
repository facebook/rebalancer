# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

import unittest

from rebalancer.explorer.client.server_selection import select_best_server, ServerAddr
from rebalancer.explorer.explorer.thrift_types import ServerStatus


def _status(loading: int, free_bytes: int) -> ServerStatus:
    return ServerStatus(
        loadingSandboxCount=loading,
        loadedSandboxCount=0,
        freeMemoryBytes=free_bytes,
        usedMemoryBytes=0,
    )


_ADDRS = [
    ServerAddr("a", 1),
    ServerAddr("b", 2),
    ServerAddr("c", 3),
]


class SelectBestServerTest(unittest.TestCase):
    def test_picks_fewest_loading(self) -> None:
        statuses = [_status(3, 100), _status(1, 10), _status(2, 100)]
        self.assertEqual(ServerAddr("b", 2), select_best_server(statuses, _ADDRS))

    def test_ties_broken_by_most_free_memory(self) -> None:
        statuses = [_status(1, 50), _status(1, 300), _status(1, 100)]
        self.assertEqual(ServerAddr("b", 2), select_best_server(statuses, _ADDRS))

    def test_skips_unreachable_servers(self) -> None:
        statuses = [None, _status(5, 100), None]
        self.assertEqual(ServerAddr("b", 2), select_best_server(statuses, _ADDRS))

    def test_returns_none_when_all_unreachable(self) -> None:
        self.assertIsNone(select_best_server([None, None, None], _ADDRS))

    def test_returns_none_for_empty(self) -> None:
        self.assertIsNone(select_best_server([], []))
