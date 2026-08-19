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
from algopt.rebalancer.common import host_utils
from algopt.rebalancer.common.host_data import HostData
from algopt.rebalancer.common.host_utils import (
    CannotExtractHostScheme,
    clean_host,
    get_distinct_dc_and_suites,
    host_is_in_dcs_filter,
    host_is_in_region_filter,
    host_name_to_server_type,
    host_to_region,
    host_to_scheme,
    host_with_facebook,
    HostIsNotInFBUQDN,
    tier_is_in_region_filter,
)
from algopt.rebalancer.common.rack_data import RackData
from facebook.core_systems.cluster.thrift_types import ClusterType
from libfb.py import testutil


class TestHostUtils(testutil.BaseFacebookTestCase):
    def test_import(self) -> None:
        self.assertEqual(host_utils, host_utils)

    def test_clean_host(self) -> None:
        self.assertEqual(clean_host("abc231.prn1.facebook.com"), "abc231.prn1")
        self.assertEqual(clean_host("abc231.prn1"), "abc231.prn1")

    def test_host_with_facebook(self) -> None:
        self.assertEqual(host_with_facebook("abc231.prn1"), "abc231.prn1.facebook.com")

    def test_host_with_facebook_throws(self) -> None:
        with self.assertRaises(HostIsNotInFBUQDN) as cm:
            host_with_facebook("abc231.prn1.facebook.com")

        self.assertEqual(
            cm.exception.args[0].endswith(" != abc231.prn1.facebook.com"), True
        )

    def test_host_to_scheme(self) -> None:
        self.assertEqual(host_to_scheme("abc231.prn1"), "abc")
        self.assertEqual(host_to_scheme("abc.231.prn1"), "abc")

    def test_host_to_scheme_more_than_3_parts(self) -> None:
        self.assertEqual(
            host_to_scheme("sparefullweb76301.vll1.facebook.com"), "sparefullweb"
        )
        self.assertEqual(
            host_to_scheme("twshared5131.13.vll1.facebook.com"), "twshared"
        )
        self.assertEqual(
            host_to_scheme("scheme123.for.a.very.very.long.fqdn"), "scheme"
        )

    def test_host_to_scheme_to_be_allocated(self) -> None:
        self.assertEqual(
            host_to_scheme("twshared__6+SKYLAKE__to__be__allocated__2"), "twshared"
        )

    def test_host_to_scheme_it_and_facnet(self) -> None:
        self.assertEqual(host_to_scheme("csw30a-fab02.prn4.tfbnw.net"), "csw")

        with self.subTest("serf_dict_missing_cluster_obj.type"):
            with self.assertRaises(CannotExtractHostScheme):
                # pyre-fixme[6]: For 2nd argument expected `Optional[Dict[str,
                #  ClusterType]]` but got `Dict[str, str]`.
                host_to_scheme("csw30a-fab02.prn4.tfbnw.net", {"foo": "bar"})

        with self.subTest("serf_dict_wrong_cluster_obj.type"):
            with self.assertRaises(CannotExtractHostScheme):
                host_to_scheme(
                    "csw30a-fab02.prn4.tfbnw.net",
                    # pyre-fixme[6]: For 2nd argument expected `Optional[Dict[str,
                    #  ClusterType]]` but got `Dict[str, str]`.
                    {"cluster_obj.type": "3486534576"},
                )

        with self.subTest("serf_dict_facnet_cluster_obj.type"):
            self.assertEqual(
                host_to_scheme(
                    "csw30a-fab02.prn4.tfbnw.net",
                    {"cluster_obj.type": ClusterType.FACNET},
                ),
                "facnet_cluster",
            )

    def test_host_to_region(self) -> None:
        self.assertEqual(host_to_region("abc231.prn1"), "prn")
        self.assertEqual(host_to_region("abc.231.prn1"), "prn")

    def test_host_is_in_dcs_filter(self) -> None:
        self.assertEqual(host_is_in_dcs_filter(["prn1", "prn2"])("abc231.prn1"), True)
        self.assertEqual(host_is_in_dcs_filter(["prn1", "prn2"])("abc231.ftw1"), False)

    def test_host_is_in_region_filter(self) -> None:
        self.assertEqual(host_is_in_region_filter("prn")("abc231.prn1"), True)
        self.assertEqual(host_is_in_region_filter("prn")("abc231.cln1"), False)
        self.assertEqual(host_is_in_region_filter({"lla", "prn"})("abc231.prn1"), True)
        self.assertEqual(host_is_in_region_filter({"lla", "prn"})("abc231.cln1"), False)
        self.assertEqual(host_is_in_region_filter({"lla", "prn"})("abc231"), False)

    def test_tier_is_in_region_filter(self) -> None:
        self.assertEqual(tier_is_in_region_filter("ATN")(["ATN", "PRN"]), True)
        self.assertEqual(tier_is_in_region_filter("ATN")(["PRN"]), False)

    def test_get_cpu_architecture(self) -> None:
        host_data = HostData(
            serf={"part_obj.server_part_details.cpu_architecture": "FOO"}
        )
        self.assertEqual(host_utils.get_cpu_architecture(host_data), "FOO")

    def test_get_server_type(self) -> None:
        host_data = HostData(serf={"part_obj.server_part_details.server_type": "foo"})
        self.assertEqual(host_utils.get_server_type(host_data), "foo")

    def test_get_distinct_dc_and_suites_from_host_data(self) -> None:
        host_data_dict = {
            "1": HostData(serf={"datacenter": "a", "suite": "aa"}, to_allocate=False),
            "2": HostData(serf={"datacenter": "b", "suite": "bb"}, to_allocate=False),
            "3": HostData(serf={"datacenter": "b", "suite": "bb"}, to_allocate=False),
            "4": HostData(serf={"datacenter": "c", "suite": "cc"}, to_allocate=True),
        }
        result = get_distinct_dc_and_suites(host_data_dict)
        self.assertListEqual(result, [("a", "aa"), ("b", "bb")])

    def test_get_distinct_dc_and_suites_from_rack_data(self) -> None:
        host_data_dict = {
            "1": RackData(serf={"datacenter": "a", "suite": "aa"}, to_allocate=False),
            "2": RackData(serf={"datacenter": "b", "suite": "bb"}, to_allocate=False),
            "3": RackData(serf={"datacenter": "b", "suite": "bb"}, to_allocate=False),
            "4": RackData(serf={"datacenter": "c", "suite": "cc"}, to_allocate=True),
        }
        result = get_distinct_dc_and_suites(host_data_dict)
        self.assertListEqual(result, [("a", "aa"), ("b", "bb")])

    def test_host_name_to_server_type(self) -> None:
        host_name_server_type: list[tuple[str, str]] = [
            ("disagghead|2__6__to__be__allocated__0", "6"),
            ("disagghead|2__8__to__be__allocated__0", "8"),
            ("disagghead|2__1__to__be__allocated__0", "1"),
            ("disagghead|2__6F__to__be__allocated__0", "6F"),
            ("disagghead|2__10__to__be__allocated__0", "10"),
        ]

        for fake_host, expected_server_type in host_name_server_type:
            server_type: str = host_name_to_server_type(host_name=fake_host)
            self.assertEqual(expected_server_type, server_type)

    def test_derive_rack_data_from_host_data(self) -> None:
        host_datas = [
            HostData(
                serf={
                    "rack_serial": "123456",
                    "datacenter": "rnv_30",
                    "cluster": "cluster400",
                    "pod": "pod1",
                    "suite": "suite1",
                    "row": "row1",
                },
                rack="rack1",
                name="host1",
            ),
            HostData(
                #  `dict[str, Optional[str]]`.
                serf_to_match={
                    # pyrefly: ignore [bad-assignment]
                    "datacenter": None,
                    # pyrefly: ignore [bad-assignment]
                    "cluster": None,
                    "pod": "pod2",
                    "suite": "suite2",
                    "row": "row2",
                },
                rack="rack2",
                name="host2",
            ),
            HostData(
                #  `dict[str, Optional[str]]`.
                serf_to_match={
                    # pyrefly: ignore [bad-assignment]
                    "datacenter": None,
                    # pyrefly: ignore [bad-assignment]
                    "cluster": None,
                    "pod": "pod3",
                    "suite": "suite3",
                    "row": "row3",
                },
                rack="rack2",
                name="host3",
            ),
            HostData(
                serf={
                    "rack_serial": "",
                    "datacenter": "rnv_30",
                    "cluster": "cluster400",
                    "pod": "pod1",
                    "suite": "suite1",
                    "row": "row1",
                },
                rack="rack1",
                name="host4",
            ),
        ]

        result = host_utils.derive_rack_data_from_host_data(host_datas)

        expected = {
            "rack1": RackData(
                name="rack1",
                serf={
                    "datacenter": "rnv_30",
                    "region": "rnv",
                    "dc_cluster": "rnv_30.cluster400",
                    "cluster": "cluster400",
                    "pod": "pod1",
                    "suite": "suite1",
                    "row": "row1",
                },
                hosts=["host1", "host4"],
                not_accepting=False,
                to_allocate=False,
                rack_serial=123456,
                rsw="",
            ),
            "rack2": RackData(
                name="rack2",
                #  `dict[str, Optional[str]]`.
                serf={
                    # pyrefly: ignore [bad-assignment]
                    "cluster": None,
                    # pyrefly: ignore [bad-assignment]
                    "datacenter": None,
                    "pod": "pod2",
                    "suite": "suite2",
                    "row": "row2",
                },
                hosts=["host2", "host3"],
                not_accepting=False,
                to_allocate=False,
            ),
        }

        self.maxDiff = None
        self.assertEqual(expected, result)
