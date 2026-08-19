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


from algopt.rebalancer.common.state_holder import StateHolder
from algopt.rebalancer.common.utils import Holder
from facebook.tupperware.common.thrift_types import ResourceLimit
from libfb.py.testutil import BaseFacebookTestCase, data_provider
from rebalancer.fetching import state_data


class TestStateData(BaseFacebookTestCase):
    # pyre-fixme[3]: Return type must be annotated.
    def _get_host_data(
        self,
        host_to_rack: dict[str, str],
        # pyre-fixme[2]: Parameter must be annotated.
        host_to_msb=None,
        # pyre-fixme[2]: Parameter must be annotated.
        host_to_tier=None,
        # pyre-fixme[2]: Parameter must be annotated.
        host_to_rba=None,
    ):
        host_to_msb = host_to_msb or {}
        host_to_tier = host_to_tier or {}
        host_to_rba = host_to_rba or {}

        hosts_data = {
            hostname: Holder(
                name=hostname,
                rack=host_to_rack[hostname],
                serf={"sub_region_fd": host_to_msb.get(hostname)},
                arg_tier=host_to_tier.get(hostname, "tier0"),
                arg_quota_id=host_to_rba.get(hostname, "quota0"),
            )
            for hostname in host_to_rack.keys()
        }

        return hosts_data

    # pyre-fixme[2]: Parameter must be annotated.
    def _get_tasks_data(self, task_to_host, task_to_job) -> list[Holder]:
        return [
            Holder(host=task_to_host[taskname], job=task_to_job[taskname])
            for taskname in task_to_host.keys()
        ]

    # pyre-fixme[3]: Return type must be annotated.
    # pyre-fixme[2]: Parameter must be annotated.
    def _get_jobs_data(self, job_to_resources):
        return {
            jobname: Holder(
                requirements=job_to_resources[jobname], updated_requirements=None
            )
            for jobname in job_to_resources.keys()
        }

    # pyre-fixme[2]: Parameter must be annotated.
    def _validate_srf_scope(self, state, expected_scope_map: dict[str, str]) -> None:
        srf_scope = state.get_scope_by_name("sub_region_fd")
        self.assertNotNone(srf_scope)
        self.assertEqual(srf_scope.container_to_scope, expected_scope_map)

    def test_import(self) -> None:
        self.assertEqual(state_data, state_data)

    def test_sub_region_fd_rack(self) -> None:
        state = StateHolder(
            config_map={"container_name": "rack", "object_name": "host"},
            hosts_data=self._get_host_data(
                {"test123.xyz1": "xx:yy:12", "test124.xyz2": "xx:zz:01"},
                {"test123.xyz1": "XYZ1|00|MSB1", "test124.xyz2": "XYZ2|10|MSB3"},
            ),
        )
        state_data.add_sub_region_fault_domain(state)
        self._validate_srf_scope(
            state, {"xx:yy:12": "XYZ1|00|MSB1", "xx:zz:01": "XYZ2|10|MSB3"}
        )

    def test_sub_region_fd_host(self) -> None:
        host_to_msb = {"test123.xyz1": "XYZ1|00|MSB1", "test124.xyz2": "XYZ2|10|MSB3"}
        state = StateHolder(
            config_map={"container_name": "host", "object_name": "task"},
            hosts_data=self._get_host_data(
                {"test123.xyz1": "xx:yy:12", "test124.xyz2": "xx:zz:01"}, host_to_msb
            ),
        )
        state_data.add_sub_region_fault_domain(state)
        self._validate_srf_scope(state, host_to_msb)

    def test_set_task_tw_resources(self) -> None:
        hosts_data = self._get_host_data(
            host_to_rack={"test123.xyz1": "xx:yy:12", "test124.xyz2": "xx:zz:01"},
            host_to_tier={"test123.xyz1": "tier123", "test124.xyz2": "tier124"},
            host_to_rba={"test123.xyz1": "quota1", "test124.xyz2": "quota2"},
        )
        tasks_data = self._get_tasks_data(
            task_to_host={
                "test/clu/job1/0": "test123.xyz1",
                "test/clu/job2/0": "test124.xyz2",
            },
            task_to_job={
                "test/clu/job1/0": "test/clu/job1",
                "test/clu/job2/0": "test/clu/job2",
            },
        )

        job_limits = ResourceLimit(cpu=1, ram=10, disk=500, flash=0)
        jobs_data = self._get_jobs_data(
            job_to_resources={"test/clu/job1": job_limits, "test/clu/job2": job_limits}
        )
        # pyre-fixme[6]: For 1st argument expected `List[TaskData]` but got
        #  `List[Holder]`.
        state_data.set_task_tw_resources(tasks_data, jobs_data, hosts_data)

    @data_provider(
        lambda: (
            (
                {"test123.xyz1": "XYZ1|00|MSB1", "test124.xyz2": "XYZ2|10|MSB3"},
                {"xx:yy:12": "XYZ1|00|MSB1", "xx:zz:01": "XYZ2|10|MSB3"},
            ),
            ({"test123.xyz1": "XYZ1|00|MSB1"}, {"xx:yy:12": "XYZ1|00|MSB1"}),
        )
    )
    # pyre-fixme[2]: Parameter must be annotated.
    def test_get_rack_to_srf(self, hosts_msbs, expected_result) -> None:
        state = StateHolder(
            object_name="host",
            container_name="rack",
            hosts_data=self._get_host_data(
                {"test123.xyz1": "xx:yy:12", "test124.xyz2": "xx:zz:01"}, hosts_msbs
            ),
        )
        results = state_data._get_rack_to_srf(state)
        self.assertEqual(results, expected_result)
