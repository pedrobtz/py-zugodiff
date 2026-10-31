import random

import pytest

import zugodiff
from zugodiff import Run


def lcs_distance(a, b):
    """Insert/delete edit distance, O(NM): len(a) + len(b) - 2 * LCS."""
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b):
            cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return len(a) + len(b) - 2 * prev[-1]


def problems(d, a, b):
    """What is wrong with d as an edit script from a to b, or None."""
    i = j = 0
    for r in d.runs:
        if (r.old_start, r.new_start) != (i, j):
            return f"run {r} does not start at ({i}, {j})"
        if r.length <= 0:
            return f"empty run {r}"
        if r.op == "equal":
            if list(a[i : i + r.length]) != list(b[j : j + r.length]):
                return f"equal run {r} covers different items"
            i += r.length
            j += r.length
        elif r.op == "delete":
            i += r.length
        else:
            j += r.length
    if (i, j) != (len(a), len(b)):
        return f"runs end at ({i}, {j}), not ({len(a)}, {len(b)})"
    changed = sum(r.length for r in d.runs if r.op != "equal")
    if changed != d.distance:
        return f"distance {d.distance}, runs change {changed}"
    return None


def pairs(seed, n, max_len, alphabet):
    rng = random.Random(seed)
    for _ in range(n):
        a = rng.choices(alphabet, k=rng.randint(0, max_len))
        b = list(a)
        for _ in range(rng.randint(0, max_len // 2 + 1)):
            k = rng.randint(0, len(b))
            if b and rng.random() < 0.5:
                del b[min(k, len(b) - 1)]
            else:
                b.insert(k, rng.choice(alphabet))
        yield a, b


def test_paper_example():
    d = zugodiff.diff("abcabba", "cbabac", minimal=True)
    assert d.distance == 5 and d.minimal
    assert problems(d, "abcabba", "cbabac") is None


@pytest.mark.parametrize("algorithm", ["myers", "patience", "greedy"])
def test_scripts_are_valid(algorithm):
    bad = [
        (a, b, p)
        for a, b in pairs(1, 500, 40, "abcd")
        if (p := problems(zugodiff.diff(a, b, algorithm=algorithm), a, b))
    ]
    assert bad[:3] == []


def test_minimal_myers_matches_the_reference():
    bad = [
        (a, b)
        for a, b in pairs(2, 400, 30, "abc")
        if zugodiff.diff(a, b, minimal=True).distance != lcs_distance(a, b)
    ]
    assert bad[:3] == []


def test_greedy_matches_the_reference():
    bad = [
        (a, b)
        for a, b in pairs(3, 300, 30, "ab")
        if zugodiff.diff(a, b, algorithm="greedy").distance != lcs_distance(a, b)
    ]
    assert bad[:3] == []


def test_patience_is_never_shorter_than_minimal():
    bad = [
        (a, b)
        for a, b in pairs(4, 300, 30, "abcdef")
        if zugodiff.diff(a, b, algorithm="patience").distance < lcs_distance(a, b)
    ]
    assert bad[:3] == []


def test_bytes_and_ids_agree():
    bad = []
    for a, b in pairs(5, 300, 40, "xyz"):
        ba, bb = "".join(a).encode(), "".join(b).encode()
        if zugodiff.diff(ba, bb).runs != zugodiff.diff(list(ba), list(bb)).runs:
            bad.append((a, b))
    assert bad[:3] == []


def test_a_cost_limit_can_make_scripts_non_minimal():
    rng = random.Random(6)
    a = rng.choices(range(50), k=3000)
    b = rng.choices(range(50), k=3000)
    d = zugodiff.diff(a, b, max_cost=2)
    assert problems(d, a, b) is None
    assert not d.minimal
    assert zugodiff.diff(a, b, minimal=True).minimal


def test_equality_is_pythons():
    # 1, 1.0 and True hash and compare equal; "1" does not.
    d = zugodiff.diff([1, "1", (1, 2)], [True, 1.0, (1, 2)], minimal=True)
    assert d.runs[0] == Run("equal", 0, 0, 1)
    assert d.distance == 2


def test_unhashable_items_raise():
    with pytest.raises(TypeError):
        zugodiff.diff([[1]], [[1]])


def test_empty_inputs():
    assert zugodiff.diff([], []).runs == []
    assert zugodiff.diff([], [1, 2]).runs == [Run("insert", 0, 0, 2)]
    assert zugodiff.diff(b"ab", b"").runs == [Run("delete", 0, 0, 2)]


def test_bytes_and_sequences_do_not_mix():
    with pytest.raises(TypeError):
        zugodiff.diff(b"ab", [97, 98])


def test_bad_arguments():
    with pytest.raises(ValueError):
        zugodiff.diff([1], [2], algorithm="histogram")
    with pytest.raises(ValueError):
        zugodiff.diff([1], [2], max_cost=0)


def test_lines_keep_their_endings():
    d = zugodiff.diff_lines("a\nb\r\nc\n", "a\nb\nc\n")
    assert [r.op for r in d.runs] == ["equal", "delete", "insert", "equal"]


def test_identical_long_inputs():
    a = list(range(100_000))
    assert zugodiff.diff(a, list(a)).runs == [Run("equal", 0, 0, 100_000)]
