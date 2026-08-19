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

import collections.abc
import inspect
import logging
import os
import pickle
import time
from typing import Any, Callable, TypeVar, Union

import xxhash
from facebook.core_systems.queries.thrift_types import BinaryExpression
from libfb.py.timeutil import Timer
from pyre_extensions import DecoratorFactory, ParameterSpecification

_P = ParameterSpecification("P")
# pyrefly: ignore [invalid-type-var]
_R = TypeVar("R")

"""

Decorator @rebalancer_local_cache should be only used for functions that:

1) single execution of a function takes a long time (in seconds).
2) it does not modify its arguments.
3) it does not modify global variables in python.
4) only performs read-only thrift or database queries.
   (basically, pure function except for read-only external queries)
5) arguments are simple (see function _normalize_argument() below)
6) returns simple types or objects that can be pickled
   (returns no lambdas, no class defs, no func defs, no types, etc.)
"""


# pyre-fixme[3]: Return type must be annotated.
# pyre-fixme[2]: Parameter must be annotated.
def _normalize_argument(arg):
    if arg is None or isinstance(arg, (float, int, str, bool)):
        return arg
    elif isinstance(arg, tuple):
        return tuple(_normalize_argument(x) for x in arg)
    elif isinstance(arg, list):
        return [_normalize_argument(x) for x in arg]
    elif isinstance(arg, set) or isinstance(arg, collections.abc.KeysView):
        return sorted(_normalize_argument(x) for x in arg)
    elif isinstance(arg, BinaryExpression):
        return BinaryExpression(
            name=arg.name, comparison=arg.comparison, values=sorted(arg.values)
        )
    elif isinstance(arg, dict):
        return sorted(arg.items())
    else:
        raise Exception(
            "Unsupported argument type: {} [{}]".format(type(arg), str(type(arg)))
        )


_MEMOIZE_FILE_DEFAULT_CACHE_PATH: str = os.path.join("/", "var", "tmp")


# Extracted from memoize_file
class _Cacher:
    def __init__(
        self,
        fn_name: str,
        # pyre-fixme[2]: Parameter must be annotated.
        cache_version,
        ttl: int = 15 * 60,  # 15 min
        # pyre-fixme[2]: Parameter must be annotated.
        pickler=pickle,
        cache_path: Union[
            os.PathLike[bytes], os.PathLike[str], bytes, str
        ] = _MEMOIZE_FILE_DEFAULT_CACHE_PATH,
        cache_prefix: str = "memoize_file",
        # pyre-fixme[2]: Parameter must be annotated.
        logger=None,
    ) -> None:
        self.ttl = ttl
        self.pickler = pickler
        cache_filename = "{cache_prefix}-{uid}-{fn_name}-{cache_version}.pickle".format(
            cache_prefix=cache_prefix,
            uid=os.getuid(),
            fn_name=fn_name,
            cache_version=cache_version,
        )

        os.makedirs(cache_path, exist_ok=True)
        # pyre-fixme[4]: Attribute must be annotated.
        # pyre-fixme[6]: For 1st argument expected `Union[PathLike[str], str]` but
        #  got `Union[PathLike[bytes], PathLike[str], bytes, str]`.
        self.cache_path_ = os.path.join(cache_path, cache_filename)
        self.logger_ = logger or logging.getLogger(__name__)

    def checked_cached_file(self) -> tuple[bool, Any]:
        loaded_from_cache = False
        val = None

        if (
            os.path.isfile(self.cache_path_)
            and os.stat(self.cache_path_).st_mtime > time.time() - self.ttl
        ):
            self.logger_.debug("Reading cache file: {}".format(self.cache_path_))

            with Timer() as timer:
                try:
                    with open(self.cache_path_, "rb") as cache:
                        val = self.pickler.load(cache)
                    loaded_from_cache = True
                except Exception:
                    print("FAILED to load cache file")
                    self.logger_.warn("memoize_file: FAILED to load cache")

            self.logger_.debug(
                "memoize_file: cache load in {:.2f}s".format(timer.result)
            )

        return (loaded_from_cache, val)

    # pyre-fixme[2]: Parameter must be annotated.
    def write_result_to_cache(self, val) -> None:
        try:
            with open(self.cache_path_, "wb") as cache:
                self.pickler.dump(val, cache)
        except Exception:
            self.logger_.warning(
                "memoize_file: FAILED to save cache file %s", self.cache_path_
            )


# pyre-fixme[2]: Parameter must be annotated.
def _unique_call_str(version: str, args, kwargs) -> str:
    version_with_args = (
        repr(_normalize_argument(version))
        + repr(_normalize_argument(args))
        + repr(_normalize_argument(kwargs))
    )
    return xxhash.xxh3_64(version_with_args).hexdigest()


def _rebalancer_local_cache(
    ttl: int = 24 * 60 * 60,
    version: str = "",
    cache_path: str = _MEMOIZE_FILE_DEFAULT_CACHE_PATH,
) -> DecoratorFactory:
    # pyrefly: ignore [bad-specialization, not-a-type]
    def decorator(func: Callable[_P, _R]) -> Callable[_P, _R]:
        if inspect.iscoroutinefunction(func):
            # pyrefly: ignore [not-a-type]
            async def wrapper(*args: _P.args, **kwargs: _P.kwargs) -> _R:
                cacher = _Cacher(
                    fn_name=func.__name__,
                    cache_version=_unique_call_str(version, args, kwargs),
                    ttl=ttl,
                    cache_path=cache_path,
                    cache_prefix="rebalancer_local_cache_async",
                )

                (loaded_from_cache, val) = cacher.checked_cached_file()

                if not loaded_from_cache:
                    val = await func(*args, **kwargs)

                    cacher.write_result_to_cache(val)

                return val

            return wrapper
        else:
            # pyrefly: ignore [not-a-type]
            def wrapper(*args: _P.args, **kwargs: _P.kwargs) -> _R:
                ucs = _unique_call_str(version, args, kwargs)
                cacher = _Cacher(
                    fn_name=func.__name__,
                    cache_version=ucs,
                    ttl=ttl,
                    cache_path=cache_path,
                    cache_prefix="rebalancer_local_cache",
                )
                (loaded_from_cache, val) = cacher.checked_cached_file()

                if not loaded_from_cache:
                    val = func(*args, **kwargs)

                    cacher.write_result_to_cache(val)

                return val

            return wrapper

    return decorator


# pyre-fixme[3]: Return type must be annotated.
# pyre-fixme[2]: Parameter must be annotated.
def _rebalancer_noop_cache(**kwargs):
    return lambda func: func


if (
    os.environ.get("REBALANCER_LOCAL_CACHE", "").lower() == "true"
    or os.environ.get("REBALANCER_LOCAL_CACHE") == "1"
):
    rebalancer_local_cache = _rebalancer_local_cache
else:
    rebalancer_local_cache = _rebalancer_noop_cache
