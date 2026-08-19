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

import json
import os
from tempfile import TemporaryDirectory

from algopt.rebalancer.common.parse_config_utils import (
    ConstraintAttrs,
    get_all_host_names,
    get_all_unmovable_hosts,
    is_useful_constraint,
    parse_constraint_type,
    parse_rebalancer_config_create_map,
)
from libfb.py.testutil import BaseFacebookTestCase, data_provider
from rebalancer.interface.thrift.Types.thrift_types import SingleConstraintSummary


def _create_test_file(
    tmp_dir: TemporaryDirectory[str],
    file_name: str,
    # pyre-fixme[2]: Parameter must be annotated.
    contents,
) -> None:
    with open(os.path.join(tmp_dir.name, file_name), "wb") as f:
        f.write(json.dumps(contents).encode())


class ParseConfigUtilsTest(BaseFacebookTestCase):
    """
    Test methods in parse utils
    """

    @data_provider(
        lambda: (
            {
                "name": "AvailabilityConstraint: max 1 hosts in sub_region_fd",
                "expected_type": "AvailabilityConstraint",
            },
            {
                "name": "isolateconstraint: max 1 hosts in sub_region_fd",
                "expected_type": "IsolateConstraint",
            },
        )
    )
    # pyre-fixme[2]: Parameter must be annotated.
    def test_parse_constraint_type(self, name: str, expected_type) -> None:
        result = parse_constraint_type(name)
        self.assertEqual(result, expected_type)

    #  "name":"AvailabilityConstraint: max 1 hosts in sub_region_fd","is_useful":True
    #  }, { "name":"capacityconstraint: max 1 hosts in sub_region_fd","is_useful":False
    #  }))` to decorator factory `libfb.py.testutil.data_provider`.
    @data_provider(
        lambda: (
            {
                "name": "AvailabilityConstraint: max 1 hosts in sub_region_fd",
                "is_useful": True,
            },
            {
                "name": "capacityconstraint: max 1 hosts in sub_region_fd",
                "is_useful": False,
            },
        )
    )
    def test_is_useful_constraint(self, name: str, is_useful: bool) -> None:
        result = is_useful_constraint(name)
        self.assertEqual(result, is_useful)

    @data_provider(
        lambda: (
            {
                "contents": {
                    "finalConstraint": {
                        "constraints": [
                            {
                                "name": (
                                    "IsolateConstraint: use full-sub_region_fd "
                                    "to place hosts of scheme"
                                ),
                                "desc": "description",
                                "value": 6900,
                            },
                            {
                                "name": (
                                    "AvailabilityConstraint: max 9 hosts of "
                                    "sub_region_fd"
                                ),
                                "value": 0.345,
                                "desc": "description",
                            },
                        ]
                    }
                },
                "expected": [
                    SingleConstraintSummary(
                        name=(
                            "IsolateConstraint: use full-sub_region_fd "
                            "to place hosts of scheme"
                        ),
                        desc="description",
                        value=6900,
                    ),
                    SingleConstraintSummary(
                        name=("AvailabilityConstraint: max 9 hosts of sub_region_fd"),
                        desc="description",
                        value=0.345,
                    ),
                ],
            },
        )
    )
    @data_provider(
        lambda: (
            {
                "contents": {
                    "constraints": [
                        {
                            "name": (
                                "IsolateConstraint: use full-sub_region_fd "
                                "to place hosts of scheme"
                            ),
                            "partition_name": "scheme",
                            "scope": "sub_region_fd",
                            "type": "group_exact_limit_after_move",
                        },
                        {
                            "name": (
                                "AvailabilityConstraint: max 9 hosts of "
                                "sub_region_fd_twshared_ceahold@1 in each "
                                "sub_region_fd"
                            ),
                            "partition_name": "twshared_ceahold@1",
                            "scope": "sub_region_fd",
                            "limit_ratio": 0.03,
                            "type": "group_limit_after_move",
                        },
                    ],
                    "object_partitions": {
                        "scheme": {"partition_file": "/a/b/c.json"},
                        "twshared_ceahold@1": {"partition_file": "f.json"},
                    },
                },
                "expected": {
                    (
                        "IsolateConstraint: use full-sub_region_fd "
                        "to place hosts of scheme"
                    ): ConstraintAttrs(
                        groups_file_name="c.json",
                        object_group_name="scheme",
                        scope="sub_region_fd",
                        constraint_type="group_exact_limit_after_move",
                    ),
                    (
                        "AvailabilityConstraint: max 9 hosts of "
                        "sub_region_fd_twshared_ceahold@1 in each "
                        "sub_region_fd"
                    ): ConstraintAttrs(
                        groups_file_name="f.json",
                        object_group_name="twshared_ceahold@1",
                        scope="sub_region_fd",
                        constraint_type="group_limit_after_move",
                        limit_ratio=0.03,
                    ),
                },
            },
        )
    )
    # pyre-fixme[2]: Parameter must be annotated.
    def test_parse_rebalancer_config_create_map(self, contents, expected) -> None:
        tmp_dir = TemporaryDirectory()
        _create_test_file(tmp_dir, "rebalancer.config.json", contents)

        config_map = parse_rebalancer_config_create_map(tmp_dir)
        self.assertEqual(config_map, expected)

        tmp_dir.cleanup()

    #  "file_name":"a.json","contents":{ "s1":["host1", "host2"],"s2":["host3",
    #  "host4"] },"expected":["host1", "host2", "host3", "host4"] }))` to decorator
    #  factory `libfb.py.testutil.data_provider`.
    @data_provider(
        lambda: (
            {
                "file_name": "a.json",
                "contents": {"s1": ["host1", "host2"], "s2": ["host3", "host4"]},
                "expected": ["host1", "host2", "host3", "host4"],
            },
        )
    )
    # pyre-fixme[2]: Parameter must be annotated.
    def test_get_all_host_names(self, file_name: str, contents, expected) -> None:
        tmp_dir = TemporaryDirectory()
        _create_test_file(tmp_dir, file_name, contents)

        hosts = get_all_host_names(tmp_dir, file_name)
        self.assertEqual(hosts, expected)

        tmp_dir.cleanup()

    #  "contents":["host1", "host2"] }, { "contents":[] }))` to decorator factory
    #  `libfb.py.testutil.data_provider`.
    @data_provider(lambda: ({"contents": ["host1", "host2"]}, {"contents": []}))
    # pyre-fixme[2]: Parameter must be annotated.
    def test_get_all_unmovable_hosts(self, contents) -> None:
        tmp_dir = TemporaryDirectory()
        _create_test_file(tmp_dir, "avoid_moving_hosts.json", contents)

        hosts = get_all_unmovable_hosts(tmp_dir)
        self.assertEqual(hosts, contents)

        tmp_dir.cleanup()
