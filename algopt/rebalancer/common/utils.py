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

import argparse
import asyncio
import collections.abc
import fcntl
import itertools
import json
import logging
import operator
import os
import sys
import threading
import time
from typing import (
    Any,
    Callable,
    Container,
    Iterable,
    Optional,
    Protocol as TypingProtocol,
    Sequence,
    TypeVar,
    Union,
)

from libfb.py.log import get_log_level, setup_cpp_glog
from pyre_extensions import ParameterSpecification

_CREATED_FILES: set[str] = set()

_P = ParameterSpecification("P")
# pyrefly: ignore [invalid-type-var]
_R = TypeVar("U")
# pyrefly: ignore [invalid-type-var]
_T = TypeVar("T")
# pyrefly: ignore [invalid-type-var]
_U = TypeVar("U")
# pyrefly: ignore [invalid-type-var]
_V = TypeVar("V")


class Holder:
    def __init__(self, **kwargs: Any) -> None:
        for name, value in kwargs.items():
            setattr(self, name, value)

    def __repr__(self) -> str:
        return (
            "Holder("
            + ",".join(str(k) + ":" + str(v) for k, v in sorted(self.__dict__.items()))
            + ")"
        )

    def __hash__(self) -> int:
        raise NotImplementedError(
            "Tried calling Holder.__hash__, generally sign of a bug in code"
        )

    # pyrefly: ignore [bad-override]
    def __eq__(self, other: "Holder") -> bool:
        raise NotImplementedError(
            "Tried calling Holder.__eq__, generally sign of a bug in code"
        )

    def __add__(self, other: "Holder") -> "Holder":
        for k, v in other.__dict__.items():
            setattr(self, k, v)
        return self


class FolderLock:
    """
    Locks a folder to synchronize potentially conflicting operations.
    The default mode of operation is to wait for the lock. If fail_fast
    flag is set, we fail immediately if the lock cannot be acquired.
    """

    def __init__(self, folder: str, fail_fast: bool = False) -> None:
        self.folder = folder
        self.thread_lock = threading.Lock()
        self.fail_fast = fail_fast
        # pyre-fixme[4]: Attribute must be annotated.
        self.handle = None

    def __enter__(self) -> "FolderLock":
        logging.info("Acquiring lock for " + self.folder)
        self.handle = open(self.folder + "/lockfile", "w+")
        operations = fcntl.LOCK_EX
        # This *might* work, but is actually pretty unreliable, especially since locks
        # are per-process
        if self.fail_fast:
            operations |= fcntl.LOCK_NB  # pragma: nocover

        fcntl.lockf(self.handle, operations)
        self.thread_lock.acquire()
        return self

    # pyre-fixme[2]: Parameter must be annotated.
    def __exit__(self, *args) -> None:
        logging.info("Releasing lock for " + self.folder)
        global _CREATED_FILES
        _CREATED_FILES = set()
        self.thread_lock.release()
        # pyrefly: ignore [missing-attribute]
        self.handle.close()


def checkState(state: Any, msg: str = "failed check", *args: Any) -> bool:
    """
    Checks that the given state is true. If not, throws a formatted Exception
    including the given message, of "failed check" by default, and any
    additional given args.

    state: Anything that can be 'not'-ed to get a boolean value
    msg:   Optional message to include in the Exception
    args:  Additional data to be included in the Exception message

    returns: True, indicating it passed the check. If it doesn't pass, an
             Exception is raised.
    """
    if not state:
        raise AssertionError(str(msg) % tuple(args))
    return True


def checkEqual(val1: _T, val2: _T, msg: str = "failed check", *args: Any) -> bool:
    if val1 == val2:
        return True
    raise AssertionError(("%s != %s, " % (val1, val2)) + (str(msg) % tuple(args)))


def checkIn(
    key: _T, data: Container[_T], msg: str = "failed check", *args: Any
) -> bool:
    if key in data:
        return True
    raise AssertionError(("%s missing, " % (key)) + (str(msg) % tuple(args)))


# pyrefly: ignore [bad-specialization, not-a-type]
def isLambda(func: Callable[_P, _R]) -> bool:
    return func.__name__ == "<lambda>"


def isString(val: _T) -> bool:
    return isinstance(val, str)


def isIterableNotString(val: _T) -> bool:
    return isinstance(val, collections.abc.Iterable) and not isString(val)


def isSequence(val: _T) -> bool:
    return isinstance(val, collections.abc.Sequence) and not isString(val)


def isMapping(val: _T) -> bool:
    return isinstance(val, collections.abc.Mapping)


def toListWrapIfOne(val: _T | str) -> list[_T] | list[str]:
    if isinstance(val, str):
        return [val]
    if isinstance(val, collections.abc.Iterable):
        return list(val)
    return [val]


def toSetIfNotOrNone(val: _T | set[_T] | None) -> Optional[set[_T]]:
    if val is None:
        return None
    if isinstance(val, collections.abc.Set):
        return val
    # pyre-fixme[6]: For 1st argument expected `Iterable[_T]` but got `_T`.
    return set(val)


def lambdaList(data_list: list[_T]) -> Callable[[Any], list[_T]]:
    return lambda _: data_list


# OBSOLETE: Use lambdaList()
def toListOfStringsExpressionString(data: Union[str, Iterable[str]]) -> str:
    return "[" + ",".join("'" + str(val) + "'" for val in toListWrapIfOne(data)) + "]"


def toSetOfStringsExpressionString(data: Union[str, Iterable[str]]) -> str:
    return (
        "set([" + ",".join("'" + str(val) + "'" for val in toListWrapIfOne(data)) + "])"
    )


def toMapOfStringsExpressionString(data: dict[_T, _U]) -> str:
    return repr({str(key): str(val) for key, val in data.items()})


def getOnlyElement(values: Iterable[_T]) -> _T:
    values = list(values)
    checkState(len(values) == 1, "Doesn't have exactly one element: %s", values)
    return next(iter(values))


def getElementAllIdentical(values: Iterable[_T]) -> _T:
    first = next(iter(values))
    for v in values:
        checkEqual(v, first)
    return first


def dictUnion(x: dict[_T, _U], y: dict[_T, _U]) -> dict[_T, _U]:
    """Given two dicts, merge them into a new dict as a shallow copy."""
    z = x.copy()
    z.update(y)
    return z


def groupPairsByFirst(tuples: Iterable[tuple[_T, _U]]) -> dict[_T, list[_U]]:
    return {
        key: [item[1] for item in group]
        for key, group in itertools.groupby(
            sorted(tuples, key=operator.itemgetter(0)), operator.itemgetter(0)
        )
    }


def getSubLists(a_list: list[_T], sub_list_size: int) -> list[list[_T]]:
    return [
        a_list[offset : offset + sub_list_size]
        for offset in range(0, len(a_list), sub_list_size)
    ]


def valueToKeys(dictionary: dict[_T, _U]) -> dict[_U, list[_T]]:
    return groupPairsByFirst((value, key) for key, value in dictionary.items())


def aggregatePairsByFirst(
    pairs: Iterable[tuple[_T, _U]], merge_op: Callable[[list[_U]], _V]
) -> dict[_T, _V]:
    return {key: merge_op(values) for key, values in groupPairsByFirst(pairs).items()}


def addLoggingArguments(full_parser: argparse.ArgumentParser) -> None:
    parser = full_parser.add_argument_group(title="Logging arguments")
    parser.add_argument("--logging", default="info", help="Level of logging to use")


def initLogging() -> None:
    setup_cpp_glog(escape_newlines=False)
    parser = argparse.ArgumentParser(add_help=False)
    addLoggingArguments(parser)
    args, _rest = parser.parse_known_args()
    logging.getLogger().setLevel(get_log_level(args.logging))


def writeJsonToFile(file: str, content: Any) -> None:
    with open(file, "w") as handle:
        json.dump(content, handle, sort_keys=True, indent=4)


def createJsonFile(folder: str, name: str, content: Any) -> str:
    file_name = folder + ("" if folder.endswith("/") else "/") + name + ".json"
    checkState(
        file_name not in _CREATED_FILES,
        "File %s already created in this execution",
        file_name,
    )
    _CREATED_FILES.add(file_name)
    writeJsonToFile(file_name, content)
    return file_name


def readFromJsonFile(file: str) -> Any:
    with open(file) as handle:
        try:
            return json.load(handle)
        except ValueError:  # pragma: nocover
            logging.error("Couldn't load JSON from file: " + file)
            raise


def createFolder(folder: str) -> None:
    if not os.path.exists(folder):
        try:
            os.makedirs(folder)
        except OSError:  # pragma: nocover
            pass


class _FolderCLIArgs(TypingProtocol):
    tmp_folder: str


def folder_from_options(service: str, scope: str, options: _FolderCLIArgs) -> str:
    folder = options.tmp_folder + "/rebalancer/" + service + "/" + scope + "/"
    createFolder(folder)

    checkState(os.path.exists(folder), folder)
    return folder


def promptUserChoice(message: str, choices: Container[str]) -> str:
    while True:
        sys.stdout.write(message)
        sys.stdout.flush()

        response = sys.stdin.readline().strip().lower()

        if response in choices:
            return response
        else:
            sys.stdout.write("unknown choice, try again\n")


def promptUserYN(message: str) -> bool:
    yes = ["y", "yes"]
    no = ["n", "no"]
    response = promptUserChoice(message + " [y/n] ", set(yes + no))
    return response in yes


def retryFunction(
    f: Callable[[], _R],
    name: str,
    sleep_before_retry: Sequence[int] = (0, 1, 4),
) -> _R:
    """Retries len(sleep_before_retry) times, so calls f one more time"""
    msg = ""
    for i in range(len(sleep_before_retry) + 1):
        try:
            return f()
        except Exception as ex:
            logging.exception("Failed to fetch %s on try %d: %s" % (name, i, str(ex)))
            msg = str(ex)
            if i < len(sleep_before_retry):
                time.sleep(sleep_before_retry[i])

    logging.error(
        "Cannot fetch %s, failed %d times, %s"
        % (name, len(sleep_before_retry) + 1, msg)
    )
    raise


def regexListMatchExpr(
    strings: Iterable[str], max_choices: int = 4, backslash: str = "\\"
) -> str:
    """
    Given a list of strings, produce an efficient regex that matches it.
    For example, for:
    strings=["abc", "abd", "aaa"]
    regex will be "a(aa|b(c|d))"

    Regex is grouped from left to right, but common ending is extracted,
    for example:

    strings=["abcyx", "abdzx", "aaazx"]
    regex will be "a(aaz|b(cy|dz))x"

    If next letter has more then max_choices values, we use a *,
    and regex will match items outside of input list,
    but will be more efficient.
    """
    dot = backslash + "."

    strings = sorted(strings)
    max_len = max(len(x) for x in strings)
    min_len = min(len(x) for x in strings)
    checkState(min_len == max_len, str(strings))
    prefix = os.path.commonprefix(strings)
    if len(prefix) == max_len:
        return strings[0].replace(".", dot)

    suffix = os.path.commonprefix([x[::-1] for x in strings])
    checkState(len(prefix) + len(suffix) < max_len)

    by_first_char = groupPairsByFirst(
        (x[len(prefix)], x[(len(prefix) + 1) : len(x) - len(suffix)]) for x in strings
    )

    # faster to get more results, then to need to iteratively check more choices
    if max_choices and len(by_first_char) > max_choices:
        middle = "." + regexListMatchExpr(
            [x[(len(prefix) + 1) : len(x) - len(suffix)] for x in strings],
            max_choices=max_choices,
            backslash=backslash,
        )
    else:
        middle = "|".join(
            (dot if c == "." else c)
            + regexListMatchExpr(rest, max_choices=max_choices, backslash=backslash)
            for c, rest in by_first_char.items()
        )
        middle = "(" + middle + ")"

    return (
        strings[0][: len(prefix)].replace(".", dot)
        + middle
        + strings[0][len(strings[0]) - len(suffix) :].replace(".", dot)
    )


def textHead(text: str, max_lines: int = 1000) -> str:
    lines = text.split("\n")
    count = len(lines)
    if count <= max_lines:
        return text
    else:
        return "{lines}\n{etc}".format(
            lines="\n".join(lines[:max_lines]),
            etc="... and %d more lines" % (count - max_lines),
        )


def listHead(items: Container[_T], max_items: int = 10) -> str:
    # pyre-fixme[6]: For 1st argument expected `pyre_extensions.PyreReadOnly[Sized]`
    #  but got `Container[Any]`.
    count = len(items)
    return "{items}{etc}".format(
        # pyre-fixme[6]: Expected `Iterable[Variable[itertools._T]]` for 1st param
        #  but got `Container[typing.Any]`.
        items=", ".join(itertools.islice(items, max_items)),
        etc="" if count <= max_items else "... (%d total items)" % count,
    )


def getServerTypeString(
    server_type: int, flash_capacity: str, cpu_architecture: Optional[str] = None
) -> str:
    server_type_str = str(server_type)
    if server_type_str == "6" and int(flash_capacity) > 0:
        server_type_str += "f"
    if cpu_architecture is not None:
        server_type_str += "+" + cpu_architecture
    return server_type_str


# pyre-fixme[24]: Generic type `Iterable` expects 1 type parameter.
def get_diff(itr1: Iterable, itr2: Iterable) -> str:
    """
    Prints difference in two iterables in diff format of + and -
    """
    missing_str = extra_str = ""
    missing = set(itr1) - set(itr2)
    if missing:
        missing_str = "- " + "\n- ".join(sorted(missing))

    extra = set(itr2) - set(itr1)
    if extra:
        extra_str = "+ " + "\n+ ".join(sorted(extra))

    return missing_str + "\n" + extra_str


def get_event_loop() -> asyncio.AbstractEventLoop:
    try:
        return asyncio.get_running_loop()
    except RuntimeError:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        return loop
