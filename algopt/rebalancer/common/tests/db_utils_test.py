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
from datetime import datetime
from unittest.mock import Mock, patch

from algopt.rebalancer.common import db_utils
from libfb.py.testutil import BaseFacebookTestCase, data_provider


BASE_PATH = "algopt.rebalancer.common.db_utils"


class TestDBUtils(BaseFacebookTestCase):
    @data_provider(
        lambda: (
            {
                "region": "atn",
                "server_type": None,
                "mock_return": [
                    {
                        "region": "atn",
                        "server_type": "5",
                        "uuid": "abc123",
                        "creation_time": datetime.strptime(
                            "2019-08-21 11:00:00", "%Y-%m-%d %H:%M:%S"
                        ),
                        "num_swaps": 0,
                    }
                ],
                "expected_result": [
                    db_utils.ContinuousBalanceMetaData(
                        region="atn",
                        server_type="5",
                        run_uuid="abc123",
                        creation_time=datetime.strptime(
                            "2019-08-21 11:00:00", "%Y-%m-%d %H:%M:%S"
                        ),
                        num_swaps=0,
                    )
                ],
                "server_type_in_where_clause": False,
                "where_clause": True,
            },
            {
                "region": "atn",
                "server_type": "1",
                "mock_return": [
                    {
                        "region": "atn",
                        "server_type": "1",
                        "uuid": "abc123",
                        "creation_time": datetime.strptime(
                            "2019-08-21 11:00:00", "%Y-%m-%d %H:%M:%S"
                        ),
                        "num_swaps": 100,
                    }
                ],
                "expected_result": [
                    db_utils.ContinuousBalanceMetaData(
                        region="atn",
                        server_type="1",
                        run_uuid="abc123",
                        creation_time=datetime.strptime(
                            "2019-08-21 11:00:00", "%Y-%m-%d %H:%M:%S"
                        ),
                        num_swaps=100,
                    )
                ],
                "server_type_in_where_clause": True,
                "where_clause": True,
            },
            {
                "region": None,
                "server_type": None,
                "mock_return": [
                    {
                        "region": "atn",
                        "server_type": "1",
                        "uuid": "abc123",
                        "creation_time": datetime.strptime(
                            "2019-08-21 11:00:00", "%Y-%m-%d %H:%M:%S"
                        ),
                        "num_swaps": 0,
                    }
                ],
                "expected_result": [
                    db_utils.ContinuousBalanceMetaData(
                        region="atn",
                        server_type="1",
                        run_uuid="abc123",
                        creation_time=datetime.strptime(
                            "2019-08-21 11:00:00", "%Y-%m-%d %H:%M:%S"
                        ),
                        num_swaps=0,
                    )
                ],
                "server_type_in_where_clause": False,
                "where_clause": False,
            },
        )
    )
    def test_get_latest_continuous_run_uuids(
        self,
        region: str | None,
        server_type: str | None,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_return,
        # pyre-fixme[2]: Parameter must be annotated.
        expected_result,
        # pyre-fixme[2]: Parameter must be annotated.
        server_type_in_where_clause,
        # pyre-fixme[2]: Parameter must be annotated.
        where_clause,
    ) -> None:
        mock_smc_db = self.addPatcher(
            patch(BASE_PATH + ".smc_db.RetryableSmcConnection")
        )
        mock_query = Mock(return_value=mock_return)
        mock_smc_db.return_value.query = mock_query

        result = db_utils.get_latest_continuous_run_uuids(region, server_type)

        self.assertEqual(result, expected_result)

        call_arg = mock_query.call_args[0][0]
        if server_type_in_where_clause:
            self.assertTrue("AND server_type" in call_arg)
        else:
            self.assertTrue("AND server_type" not in call_arg)

        if where_clause:
            self.assertTrue("WHERE region = " in call_arg)
        else:
            self.assertTrue("WHERE region = " not in call_arg)

    @data_provider(
        lambda: (
            {
                "base_table": "continuous_balancing_meta_data",
                "abtest": True,
                "expected_result": "continuous_balancing_meta_data_abtest",
            },
            {
                "base_table": "continuous_balancing_meta_data",
                "abtest": False,
                "expected_result": "continuous_balancing_meta_data",
            },
        )
    )
    def test_get_table_name(
        self,
        base_table: str,
        abtest: bool,
        # pyre-fixme[2]: Parameter must be annotated.
        expected_result,
    ) -> None:
        self.assertEqual(expected_result, db_utils.get_table_name(base_table, abtest))
