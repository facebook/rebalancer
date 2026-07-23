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
import tempfile
from functools import partial
from threading import Thread
from unittest.mock import call, patch

from algopt.rebalancer.common import utils
from libfb.py import testutil


class TestUtils(testutil.BaseFacebookTestCase):
    def test_Holder(self) -> None:
        state = utils.Holder(foo="bar", this="that")
        with self.assertRaises(NotImplementedError):
            hash(state)
        with self.assertRaises(NotImplementedError):
            # Deliberately call non-existent method
            # pyre-ignore[58]: `==` not supported, but we want to test that
            state == 0  # noqa: B015
        other = utils.Holder(x="y")
        state += other
        self.assertEqual(repr(state), "Holder(foo:bar,this:that,x:y)")

    def test_FolderLock(self) -> None:
        # We're not mocking the filesystem here. Create a temp dir instead, which should
        # be hermetic enough.
        tmp = tempfile.mkdtemp()

        # pyre-fixme[2]: Parameter must be annotated.
        def try_get_lock(lock, counter_dict, stop_at) -> None:
            # Safety mechanism to avoid runaway test
            with lock:
                while True:
                    if counter_dict["counter"] >= stop_at:
                        break

        # Thread a gets lock first
        lock = utils.FolderLock(tmp, False)
        counter_dict = {"counter": 0}
        thread_a = Thread(target=partial(try_get_lock, lock, counter_dict, 1))
        thread_a.start()
        thread_b = Thread(target=partial(try_get_lock, lock, counter_dict, 1))
        thread_b.start()
        thread_b.join(timeout=1)
        # thread_b is still alive because it's waiting for that lock
        self.assertTrue(thread_b.is_alive())
        counter_dict["counter"] = 1
        thread_a.join(timeout=1)
        self.assertFalse(thread_a.is_alive())
        # Now b should finish up
        thread_b.join(timeout=1)
        self.assertFalse(thread_b.is_alive())

    def test_checkState(self) -> None:
        self.assertTrue(utils.checkState(1))
        with self.assertRaises(AssertionError):
            utils.checkState("")

    def test_checkEqual(self) -> None:
        self.assertTrue(utils.checkEqual(5, 5))
        with self.assertRaises(AssertionError):
            utils.checkEqual("foo", "bar")

    def test_checkin(self) -> None:
        self.assertTrue(utils.checkIn("bar", "foobarbaz"))
        self.assertTrue(utils.checkIn("foo", {"foo", "bar"}))
        self.assertTrue(utils.checkIn("foo", {"foo": "bar", "this": "that"}))
        with self.assertRaises(AssertionError):
            utils.checkIn("bar", {"foo": "bar", "this": "that"})
        with self.assertRaises(AssertionError):
            utils.checkIn("baz", "foobar")

    def test_isLambda(self) -> None:
        def realfunc() -> bool:
            return True

        inline = lambda: True  # noqa E731
        self.assertTrue(utils.isLambda(inline))
        self.assertFalse(utils.isLambda(realfunc))
        # pyre-fixme[6]: For 1st argument expected `(_P) -> _R` but got `Type[map]`.
        self.assertFalse(utils.isLambda(map))
        with self.assertRaises(AttributeError):
            # pyre-fixme[6]: For 1st param expected `(...) -> Any` but got `bool`.
            utils.isLambda(True)

    def test_isString(self) -> None:
        self.assertTrue(utils.isString("foo"))
        self.assertFalse(utils.isString(7))

    def test_isIterableNotString(self) -> None:
        self.assertTrue(utils.isIterableNotString(set()))
        self.assertFalse(utils.isIterableNotString("foo"))
        self.assertFalse(utils.isIterableNotString(7))

    def test_isSequence(self) -> None:
        self.assertTrue(utils.isSequence([]))
        self.assertFalse(utils.isSequence(set()))
        self.assertFalse(utils.isSequence("foo"))

    def test_isMapping(self) -> None:
        self.assertTrue(utils.isMapping({}))
        self.assertFalse(utils.isMapping([]))

    def test_toListWrapIfOne(self) -> None:
        self.assertEqual(utils.toListWrapIfOne("foo"), ["foo"])
        self.assertEqual(utils.toListWrapIfOne({"foo"}), ["foo"])

    def test_toSetIfNotOrNone(self) -> None:
        self.assertIsNone(utils.toSetIfNotOrNone(None))
        self.assertEqual(utils.toSetIfNotOrNone({"foo"}), {"foo"})
        self.assertEqual(utils.toSetIfNotOrNone(["foo"]), {"foo"})

    def test_lambdaList(self) -> None:
        func = utils.lambdaList(["foo", "bar"])
        self.assertEqual(func("baz"), ["foo", "bar"])

    def test_toListOfStringsExpressionString(self) -> None:
        self.assertEqual(utils.toListOfStringsExpressionString("foo"), "['foo']")

    def test_toSetOfStringsExpressionString(self) -> None:
        self.assertEqual(utils.toSetOfStringsExpressionString("foo"), "set(['foo'])")

    def test_toMapOfStringsExpressionString(self) -> None:
        self.assertEqual(
            utils.toMapOfStringsExpressionString({"foo": 7}), "{'foo': '7'}"
        )

    def test_getOnlyElement(self) -> None:
        with self.assertRaises(AssertionError):
            utils.getOnlyElement(["foo", "bar"])
        self.assertEqual(utils.getOnlyElement({"baz"}), "baz")
        self.assertEqual(utils.getOnlyElement({"baz": "what"}), "baz")
        self.assertEqual(utils.getOnlyElement("b"), "b")

    def test_getElementAllIdentical(self) -> None:
        with self.assertRaises(AssertionError):
            utils.getElementAllIdentical(["foo", "bar"])
        self.assertEqual(utils.getElementAllIdentical(["baz", "baz", "baz"]), "baz")

    def test_dictUnion(self) -> None:
        da = {"foo": "bar"}
        db = {"x": "y"}
        res = utils.dictUnion(da, db)
        self.assertEqual(res, {"foo": "bar", "x": "y"})
        self.assertIsNot(res, da)
        self.assertIsNot(res, db)

    def test_groupPairsByFirst(self) -> None:
        res = utils.groupPairsByFirst(
            [("foo", "bar"), ("foo", "baz"), ("x", "y"), ("x", "z")]
        )
        self.assertEqual(res, {"foo": ["bar", "baz"], "x": ["y", "z"]})

    def test_getSubLists(self) -> None:
        self.assertEqual(utils.getSubLists(list(range(5)), 2), [[0, 1], [2, 3], [4]])

    def test_valueToKeys(self) -> None:
        self.assertEqual(
            utils.valueToKeys({"foo": "bar", "x": "y", "baz": "bar"}),
            {"bar": ["foo", "baz"], "y": ["x"]},
        )

    def test_aggregatePairsByFirst(self) -> None:
        self.assertEqual(
            utils.aggregatePairsByFirst([(0, 1), (2, 3)], utils.getOnlyElement),
            {0: 1, 2: 3},
        )
        self.assertEqual(
            # pyre-fixme[6]: For 1st param expected `Iterable[tuple[Variable[_T],
            #  Variable[_U]]]` but got `list[tuple[int, int]]`.
            utils.aggregatePairsByFirst([(0, 1), (0, 2), (3, 4)], sum),
            {0: 3, 3: 4},
        )

    def test_json_file_helpers(self) -> None:
        data = {"foo": "bar", "baz": [0, 1, 2]}
        tmp = tempfile.mkdtemp()
        fname = utils.createJsonFile(tmp, "test", data)
        rec_data = utils.readFromJsonFile(fname)
        self.assertEqual(rec_data, data)

    def test_createFolder(self) -> None:
        tmp = tempfile.mkdtemp()
        subdir = os.path.join(tmp, "some/deep/path")
        utils.createFolder(subdir)
        self.assertTrue(os.path.exists(subdir))

    # Apparently autospec breaks sys.stdout.write
    @patch("algopt.rebalancer.common.utils.sys", autospec=False)
    # pyre-fixme[2]: Parameter must be annotated.
    def test_promptUserYN(self, mock_sys) -> None:
        mock_sys.stdin.readline.side_effect = ["foo", "no", "y"]
        # First try should trigger the unknown choice message, plus then a no
        self.assertFalse(utils.promptUserYN("test msg"))
        mock_sys.stdout.write.assert_has_calls(
            [
                call("test msg [y/n] "),
                call("unknown choice, try again\n"),
                call("test msg [y/n] "),
            ]
        )
        # This time we should get a yes
        self.assertTrue(utils.promptUserYN("test msg"))

    def test_retryFunction(self) -> None:
        # pyre-fixme[2]: Parameter must be annotated.
        def func(times_to_fail, call_counter) -> int:
            call_counter[0] += 1
            if call_counter[0] <= times_to_fail:
                raise RuntimeError()
            else:
                return 7

        cc = [0]
        with self.assertRaises(RuntimeError):
            utils.retryFunction(partial(func, 4, cc), "func", [0, 0, 0])
        self.assertEqual(cc, [4])
        cc = [0]
        res = utils.retryFunction(partial(func, 3, cc), "func", [0, 0, 0])
        self.assertEqual(res, 7)

    def test_regexListMatchExpr(self) -> None:
        self.assertEqual(
            utils.regexListMatchExpr(["abc", "abd", "aaa"]), "a(aa|b(c|d))"
        )
        self.assertEqual(
            utils.regexListMatchExpr(["abcyx", "abdzx", "aaazx"]), "a(aaz|b(cy|dz))x"
        )
        self.assertEqual(
            utils.regexListMatchExpr(["abcyx", "abdzx", "aaazx"], max_choices=1),
            "a...x",
        )

    def test_textHead(self) -> None:
        self.assertEqual(utils.textHead("foo\nbar"), "foo\nbar")
        self.assertEqual(utils.textHead("foo\nbar", 1), "foo\n... and 1 more lines")

    def test_listHead(self) -> None:
        self.assertEqual(
            utils.listHead(["foo", "bar", "baz"], 2), "foo, bar... (3 total items)"
        )

    def test_getServerTypeString(self) -> None:
        # pyre-fixme[6]: For 2nd param expected `str` but got `int`.
        self.assertEqual(utils.getServerTypeString(6, 0, "foo"), "6+foo")
        # pyre-fixme[6]: For 2nd param expected `str` but got `int`.
        self.assertEqual(utils.getServerTypeString(6, 1000), "6f")

    def test_get_diff(self) -> None:
        self.assertEqual(
            utils.get_diff(["foo", "bar", "bip"], ["foo", "baz", "bop"]),
            "- bar\n- bip\n+ baz\n+ bop",
        )

    @patch("algopt.rebalancer.common.utils.asyncio", autospec=True)
    # pyre-fixme[2]: Parameter must be annotated.
    def test_get_event_loop(self, mock_asyncio) -> None:
        # First, exercise the typical case, when things to right
        mock_asyncio.get_running_loop.return_value = "foo"
        mock_asyncio.new_event_loop.return_value = "bar"
        self.assertEqual(utils.get_event_loop(), "foo")
        # But when asyncio.get_running_loop throws, utils.get_event_loop has a recovery
        # workflow. Let's exercise that as well.
        mock_asyncio.get_running_loop.side_effect = RuntimeError
        self.assertEqual(utils.get_event_loop(), "bar")
        mock_asyncio.set_event_loop.assert_called_once_with("bar")
