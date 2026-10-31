"""The runtime inside Python: failures, the data stacks and interrupts."""

import random
import signal

import pytest

import zugodiff
from zugodiff import GooseError, _core


def busy_pair():
    # A small alphabet keeps every item matched somewhere, so nothing is set
    # aside before the search, which then takes many steps (and polls).
    rng = random.Random(1)
    return rng.choices(range(4), k=20_000), rng.choices(range(4), k=20_000)


def test_a_runtime_failure_is_an_exception():
    with pytest.raises(GooseError, match="out of bounds"):
        _core.probe_index(5)
    assert _core.probe_index(1) == 20


def test_a_failure_rewinds_the_data_stacks():
    zugodiff.diff([1, 2], [2, 3])
    before = _core.stack_used()
    for _ in range(50):
        with pytest.raises(GooseError):
            _core.probe_stack(100_000, True)
    assert _core.stack_used() == before
    assert _core.probe_stack(10, False) == 9


def test_a_data_stack_overflow_is_an_exception():
    before = _core.stack_used()
    for _ in range(20):
        with pytest.raises(GooseError, match="data stack overflow"):
            _core.test_touch_gap()
    assert _core.stack_used() == before
    assert zugodiff.diff([1, 2], [2]).distance == 1


def test_ctrl_c_interrupts_a_long_diff():
    a, b = busy_pair()
    before = _core.stack_used()
    _core.test_raise_after(3)
    with pytest.raises(KeyboardInterrupt):
        zugodiff.diff(a, b, minimal=True)
    assert _core.stack_used() == before
    assert zugodiff.diff([1, 2], [2]).distance == 1


def test_a_signal_handler_runs_inside_the_call():
    a, b = busy_pair()
    seen = []
    previous = signal.signal(signal.SIGINT, lambda *_: seen.append(1))
    try:
        _core.test_raise_after(3)
        d = zugodiff.diff(a, b, minimal=True)
    finally:
        signal.signal(signal.SIGINT, previous)
    assert seen == [1]
    assert d.minimal


def test_re_entering_from_a_signal_handler_is_refused():
    a, b = busy_pair()

    def handler(*_):
        zugodiff.diff([1], [2])

    previous = signal.signal(signal.SIGINT, handler)
    try:
        _core.test_raise_after(3)
        with pytest.raises(GooseError, match="re-entered"):
            zugodiff.diff(a, b, minimal=True)
    finally:
        signal.signal(signal.SIGINT, previous)
    assert zugodiff.diff([1, 2], [2]).distance == 1


def test_goose_info():
    info = zugodiff.goose_info()
    assert len(info["goose_commit"]) == 40
    assert info["runtime_version"]
