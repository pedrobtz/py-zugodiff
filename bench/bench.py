"""zugodiff against difflib. Run from the repository root:

    python bench/bench.py

The cases follow zudiff's design §10: 10^5 lines with 1 % changed, and
byte strings with scattered edits. difflib.SequenceMatcher is not a
minimal diff (it finds longest matching blocks, Ratcliff/Obershelp), so
the edit distance each finds is shown as well.
"""

import difflib
import random
import statistics
import time

import zugodiff


def best_of(f, n=5):
    times = []
    for _ in range(n):
        t = time.perf_counter()
        f()
        times.append(time.perf_counter() - t)
    return min(times), statistics.median(times)


def difflib_distance(sm):
    return sum((i2 - i1) + (j2 - j1) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal")


def text_case(n=100_000, frac=0.01):
    rng = random.Random(20261027)
    old = [f"line {i}: {''.join(rng.choices('abcdefghijklmnopqrstuvwxyz', k=8))}\n" for i in range(n)]
    new = list(old)
    for i in rng.sample(range(n), int(n * frac)):
        new[i] = new[i][:-1] + " changed\n"
    return old, new


def bytes_case(n, edits):
    rng = random.Random(1)
    old = bytearray(rng.randbytes(n))
    new = bytearray(old)
    for i in rng.sample(range(n), edits):
        new[i] = (new[i] + 1) % 256
    return bytes(old), bytes(new)


def row(name, f_zd, f_dl):
    (zd_best, _), (dl_best, _) = best_of(f_zd), best_of(f_dl, 3)
    print(f"| {name} | {zd_best * 1000:.1f} ms | {dl_best * 1000:.1f} ms | {dl_best / zd_best:.1f}x |")


def main():
    print("| Case | zugodiff | difflib | speed-up |")
    print("| --- | --- | --- | --- |")
    old, new = text_case()
    row("10^5 lines, 1 % changed", lambda: zugodiff.diff(old, new),
        lambda: difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes())
    row("same, patience", lambda: zugodiff.diff(old, new, algorithm="patience"),
        lambda: difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes())
    a, b = bytes_case(100_000, 100)
    row("10^5 bytes, 100 edits", lambda: zugodiff.diff(a, b),
        lambda: difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes())
    a10, b10 = bytes_case(10_000_000, 100)
    zd_best, _ = best_of(lambda: zugodiff.diff(a10, b10))
    print(f"| 10^7 bytes, 100 edits | {zd_best * 1000:.1f} ms | (not run) | |")

    print()
    print("Edit distance found on the text case:")
    print(f"- zugodiff (Myers): {zugodiff.diff(old, new).distance}")
    print(f"- zugodiff (minimal): {zugodiff.diff(old, new, minimal=True).distance}")
    sm = difflib.SequenceMatcher(None, old, new, autojunk=False)
    print(f"- difflib: {difflib_distance(sm)}")


if __name__ == "__main__":
    main()
