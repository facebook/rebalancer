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
from __future__ import absolute_import, division, print_function, unicode_literals

import os
import tempfile
import threading
from argparse import Namespace

from algopt.rebalancer.common import utils
from libfb.py import testutil


class TestUtils(testutil.BaseFacebookTestCase):
    def getTmpFolder(
        self, service: str = "test_service", scope: str = "test_scope"
    ) -> str:
        options = Namespace()
        options.tmp_folder = "/tmp"
        # pyrefly: ignore [bad-argument-type]
        return utils.folder_from_options(service, scope, options)

    def test_import(self) -> None:
        self.assertEqual(utils, utils)

    def test_checkState(self) -> None:
        self.assertTrue(utils.checkState(True))

        with self.assertRaises(AssertionError) as cm:
            # pyre-ignore[6]: Required for test
            utils.checkState(False, 5)

        self.assertEqual(cm.exception.args[0], "5")

    def test_checkEqual(self) -> None:
        # message is intentionally incorrectly formatted, to fail if evaluated.
        self.assertTrue(utils.checkEqual(17, 17, "%s", 1, 2, 3))

        with self.assertRaises(AssertionError) as cm:
            utils.checkEqual(17, 2, "%s", "aba")

        self.assertEqual(cm.exception.args[0], "17 != 2, aba")

    def test_checkIn(self) -> None:
        # message is intentionally incorrectly formatted, to fail if evaluated.
        self.assertTrue(utils.checkIn(2, [1, 2, 3], "%s", 1, 2, 3))

        with self.assertRaises(AssertionError) as cm:
            utils.checkIn(2, [1, 3], "%s", "aba")

        self.assertEqual(cm.exception.args[0], "2 missing, aba")

    def test_isString(self) -> None:
        self.assertEqual(utils.isString(["a", "b", "c"]), False)
        self.assertEqual(utils.isString("abcd"), True)
        self.assertEqual(utils.isString("a" for x in range(3)), False)

    def test_isIterableNotString(self) -> None:
        self.assertEqual(utils.isIterableNotString([1, 2, 3]), True)
        self.assertEqual(utils.isIterableNotString("abcd"), False)
        self.assertEqual(utils.isIterableNotString({1, 2}), True)
        self.assertEqual(utils.isIterableNotString({1: 2, 3: 4}), True)
        self.assertEqual(utils.isIterableNotString(5), False)
        self.assertEqual(utils.isIterableNotString(x for x in range(3)), True)

    def test_isSequence(self) -> None:
        self.assertEqual(utils.isSequence([1, 2, 3]), True)
        self.assertEqual(utils.isSequence("abcd"), False)
        self.assertEqual(utils.isSequence({1, 2}), False)
        self.assertEqual(utils.isSequence({1: 2, 3: 4}), False)
        self.assertEqual(utils.isSequence(5), False)
        self.assertEqual(utils.isSequence(x for x in range(3)), False)

    def test_isMapping(self) -> None:
        self.assertEqual(utils.isMapping([1, 2, 3]), False)
        self.assertEqual(utils.isMapping("abcd"), False)
        self.assertEqual(utils.isMapping({1, 2}), False)
        self.assertEqual(utils.isMapping({1: 2, 3: 4}), True)
        self.assertEqual(utils.isMapping(5), False)
        self.assertEqual(utils.isMapping(x for x in range(3)), False)

    def test_toListWrapIfOne(self) -> None:
        self.assertEqual(utils.toListWrapIfOne([1, 2, 3]), [1, 2, 3])
        self.assertEqual(utils.toListWrapIfOne("abcd"), ["abcd"])
        self.assertEqual(utils.toListWrapIfOne(["a", "b"]), ["a", "b"])
        self.assertEqual(utils.toListWrapIfOne(5), [5])
        self.assertEqual(utils.toListWrapIfOne(x for x in range(3)), [0, 1, 2])

        self.assertEqual(utils.isSequence(utils.toListWrapIfOne({"a", "b"})), True)

    def test_toSetIfNotOrNone(self) -> None:
        self.assertEqual(utils.toSetIfNotOrNone(None), None)
        self.assertEqual(utils.toSetIfNotOrNone({1, 2}), {1, 2})
        self.assertEqual(utils.toSetIfNotOrNone([1, 2]), {1, 2})
        self.assertEqual(utils.toSetIfNotOrNone(x for x in range(2)), {0, 1})

    def test_dictUnion(self) -> None:
        self.assertEqual(utils.dictUnion({1: 1}, {2: 2}), {1: 1, 2: 2})

    def test_groupPairsByFirst(self) -> None:
        self.assertEqual(
            utils.groupPairsByFirst([(1, "a"), (1, "b"), (2, "c")]),
            {1: ["a", "b"], 2: ["c"]},
        )
        self.assertEqual(
            utils.groupPairsByFirst((x, x) for x in range(2)), {0: [0], 1: [1]}
        )

    def test_regexListMatchExpr(self) -> None:
        self.assertEqual(
            utils.regexListMatchExpr(["abc", "abd", "aaa"]), "a(aa|b(c|d))"
        )
        self.assertEqual(
            utils.regexListMatchExpr(["abcyx", "abdzx", "aaazx"]), "a(aaz|b(cy|dz))x"
        )
        self.assertEqual(utils.regexListMatchExpr(["abc", "abc", "abc"]), "abc")

    def test_folderFromOptions(self) -> None:
        folder = self.getTmpFolder()
        self.assertTrue("test_service" in folder)
        self.assertTrue("test_scope" in folder)
        self.assertTrue(os.path.exists(folder))

    def test_FolderLock(self) -> None:
        folder = self.getTmpFolder()
        with utils.FolderLock(folder):
            self.assertTrue(os.path.exists(folder + "lockfile"))

    def test_createWriteJsonFile(self) -> None:
        folder = self.getTmpFolder()
        filename = tempfile.NamedTemporaryFile(suffix="json").name.split("/")[-1]
        created_file = utils.createJsonFile(folder, filename, {})
        self.assertTrue(os.path.exists(created_file))
        self.assertEqual(utils.readFromJsonFile(created_file), {})
        utils.writeJsonToFile(created_file, [])
        self.assertEqual(utils.readFromJsonFile(created_file), [])

    def test_createJsonFileFailOverwrite(self) -> None:
        folder = self.getTmpFolder()
        utils.createJsonFile(folder, "test_json", {})
        self.assertTrue(os.path.exists(folder + "test_json.json"))
        with self.assertRaises(AssertionError):
            utils.createJsonFile(folder, "test_json", {})

    def test_createJsonFileFailParallel(self) -> None:
        def createTestJson(service: str, scope: str) -> None:
            folder = self.getTmpFolder(service, scope)
            with utils.FolderLock(folder):
                utils.createJsonFile(folder, "test_json", {})
                with self.assertRaises(AssertionError):
                    utils.createJsonFile(folder, "test_json", {})

        all_threads = []
        for i in [1, 1]:
            t = threading.Thread(
                target=createTestJson, args=("service" + str(i), "scope" + str(i))
            )
            t.start()
            all_threads.append(t)
        for t in all_threads:
            t.join()

    def test_getSubLists(self) -> None:
        a_list = list(range(100))
        sub_lists = utils.getSubLists(a_list, 15)

        self.assertEqual(len(sub_lists), 1 + len(a_list) // 15)
        # pyre-fixme[6]: For 1st param expected `Iterable[object]` but got `bool`.
        self.assertTrue(all(len(sub_list) == 15) for sub_list in sub_lists[:-1])
        self.assertEqual(len(sub_lists[-1]), 10)
        merged_list = [num for nums in sub_lists for num in nums]
        self.assertEqual(a_list, merged_list)


# def toListOfStringsExpressionString(data):
# def getOnlyElement(val):
# def valueToKeys(dictionary):
# def initLogging(options):
# def promptUserChoice(message, choices):
# def promptUserYN(message):
# def retryFunction(f, name, sleep_before_retry=[0, 1, 4] + []):
