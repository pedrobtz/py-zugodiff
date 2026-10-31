"""Myers and patience diff, with the core written in Goose.

zugodiff computes the shortest edit script between two sequences and
returns it as data: runs of the old sequence kept, deleted or inserted.
The search runs in C compiled from zudiff's Goose core
(https://github.com/pedrobtz/zudiff); this package adds the CPython glue.

Equality is Python's: items are turned into integer ids with a dict, so
two items match when they hash and compare equal, as in a set. Bytes are
compared byte by byte without ids.
"""

from __future__ import annotations

import struct
import sys
from array import array
from dataclasses import dataclass
from typing import Hashable, Literal, NamedTuple, Sequence

from . import _core
from ._core import GooseError

__all__ = ["Diff", "GooseError", "Run", "diff", "diff_lines", "goose_info"]

_ALGORITHMS = {"myers": 0, "patience": 1, "greedy": 9}
_OPS = ("equal", "delete", "insert")
_FORMAT_VERSION = 1
_NO_LIMIT = 2**63 - 1

Op = Literal["equal", "delete", "insert"]


class Run(NamedTuple):
    """One run of the edit script. Starts are 0-based."""

    op: Op
    old_start: int
    new_start: int
    length: int


@dataclass(frozen=True)
class Diff:
    """The edit script from `old` to `new`."""

    runs: list[Run]
    distance: int
    """Elements deleted plus elements inserted."""
    minimal: bool
    """False when the cost limit cut the search short."""
    algorithm: str

    def __len__(self) -> int:
        return len(self.runs)

    def __iter__(self):
        return iter(self.runs)


def _ids(a: Sequence[Hashable], b: Sequence[Hashable]) -> tuple[array, array]:
    seen: dict = {}
    get = seen.setdefault
    ia = array("i", [get(x, len(seen)) for x in a])
    ib = array("i", [get(x, len(seen)) for x in b])
    return ia, ib


def _unpack(packed: bytes, algorithm: str) -> Diff:
    version, nruns, minimal, distance = struct.unpack_from("<4i", packed)
    if version != _FORMAT_VERSION:
        raise GooseError(f"edit script format {version}, expected {_FORMAT_VERSION}")
    body = array("i")
    body.frombytes(packed[16 : 16 + 16 * nruns])
    if sys.byteorder == "big":
        body.byteswap()
    runs = [
        Run(_OPS[body[k]], body[k + 1], body[k + 2], body[k + 3])
        for k in range(0, 4 * nruns, 4)
    ]
    return Diff(runs, distance, bool(minimal), algorithm)


def diff(
    old: Sequence[Hashable] | bytes,
    new: Sequence[Hashable] | bytes,
    *,
    algorithm: Literal["myers", "patience"] = "myers",
    max_cost: int | None = None,
    minimal: bool = False,
) -> Diff:
    """The edit script from `old` to `new`.

    `old` and `new` are sequences of hashable items, or both bytes-like,
    which are compared byte by byte. Myers' search gives up on a minimal
    script after `max_cost` steps (by default GNU diff's limit, which grows
    with the input); `minimal=True` removes the limit.
    """
    if algorithm not in _ALGORITHMS:
        raise ValueError(f"algorithm must be one of {sorted(_ALGORITHMS)}, not {algorithm!r}")
    if minimal:
        cost = _NO_LIMIT
    elif max_cost is None:
        cost = -1
    elif max_cost < 1:
        raise ValueError("max_cost must be at least 1")
    else:
        cost = min(int(max_cost), _NO_LIMIT)
    byteish = (bytes, bytearray, memoryview)
    if isinstance(old, byteish) and isinstance(new, byteish):
        a, b = old, new
    elif isinstance(old, byteish) or isinstance(new, byteish):
        raise TypeError("old and new must both be bytes-like or both sequences")
    else:
        a, b = _ids(old, new)
    return _unpack(_core.diff(a, b, _ALGORITHMS[algorithm], cost), algorithm)


def diff_lines(old: str, new: str, **kwargs) -> Diff:
    """`diff()` of two strings, line by line. Line endings are kept, so a
    line that ends in "\\r\\n" differs from one that ends in "\\n"."""
    return diff(old.splitlines(keepends=True), new.splitlines(keepends=True), **kwargs)


def goose_info() -> dict[str, str]:
    """The Goose commit the core was compiled at, and the runtime hash."""
    commit, runtime = _core.goose_info()
    return {"goose_commit": commit, "runtime_version": runtime}
