"""Compares builds of zugodiff, one per git ref, for the Goose fork patches
(zudiff's .agents/goose-fork.md). Run from the repository root:

    python tools/eval_goose_fork.py [--rounds N] <ref> <ref> ...

e.g. python tools/eval_goose_fork.py main goose-hosted goose-contexts

Each ref is checked out into a temporary worktree and installed into its
own virtual environment (with uv). For each it reports the Goose pin, the
lines of first-party C against generated C, compiler warnings at -Wall,
the symbols the extension exports, and the tests. Then it times the core,
alternating between the builds for N rounds (default 6), and reports
medians and the change from the first ref. Prints Markdown.
"""

import argparse
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

GENERATED = {"zudiff_goose.c", "zudiff_goose.h", "goose_runtime.c", "goose_vendor.c",
             "goose_version.h"}

BENCH = r"""
import random, time
from array import array
from zugodiff import _core
rng = random.Random(1)
x = array("i", rng.choices(range(10_000), k=200_000))
y = array("i", x)
for i in rng.sample(range(len(y)), 2000):
    y[i] += 10_000
a = rng.randbytes(10_000_000)
b = bytearray(a)
for i in sorted(rng.sample(range(len(b)), 100)):
    b[i] = (b[i] + 1) % 256
b = bytes(b)
def med(f, n):
    t = []
    for _ in range(n):
        s = time.perf_counter(); f(); t.append(time.perf_counter() - s)
    t.sort()
    return t[len(t) // 2]
print("myers", med(lambda: _core.diff(x, y, 0, -1), 15) * 1e3)
print("patience", med(lambda: _core.diff(x, y, 1, -1), 7) * 1e3)
print("raw", med(lambda: _core.diff(a, b, 0, -1), 7) * 1e3)
n = 200_000
s = time.perf_counter()
for _ in range(n):
    _core.probe_index(1)
print("call", (time.perf_counter() - s) / n * 1e9)
"""

WHATS = {"myers": "Myers, 2e5 ids (ms)", "patience": "patience, 2e5 ids (ms)",
         "raw": "10 MB bytes (ms)", "call": "empty call (ns)"}


def run(cmd, cwd=None, check=True):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and r.returncode:
        sys.exit(f"{' '.join(map(str, cmd))} failed:\n{r.stdout}{r.stderr}")
    return r


def lines(paths):
    return sum(len(p.read_text().splitlines()) for p in paths)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("refs", nargs="+")
    opts = ap.parse_args()
    root = Path.cwd()
    builds = {}
    with tempfile.TemporaryDirectory(prefix="eval-goose-fork-") as tmp:
        tmp = Path(tmp)
        try:
            for ref in opts.refs:
                slug = re.sub(r"[^A-Za-z0-9]+", "-", ref)
                src, venv = tmp / slug, tmp / (slug + "-venv")
                run(["git", "-C", root, "worktree", "add", "--detach", src, ref])
                print(f"== {ref}: building", file=sys.stderr)
                run(["uv", "venv", "-q", venv])
                py = venv / "bin" / "python"
                inst = run(["uv", "pip", "install", "-v", "--python", py, f"{src}[test]"],
                           check=False)
                if inst.returncode:
                    sys.exit(f"build of {ref} failed:\n{inst.stderr[-3000:]}")
                gdir = src / "src" / "zugodiff" / "_goose"
                cfiles = [p for p in [*gdir.glob("*.[ch]"), *(src / "src" / "zugodiff").glob("*.c")]]
                own = [p for p in cfiles if p.name not in GENERATED]
                gen = [p for p in cfiles if p.name in GENERATED]
                # Warnings at -Wall, compiled on their own as the build does.
                warn = 0
                for c in [p for p in cfiles if p.suffix == ".c"]:
                    inc = run([py, "-c", "import sysconfig; print(sysconfig.get_paths()['include'])"]).stdout.strip()
                    r = run(["cc", "-Wall", "-O2", "-fsyntax-only", "-DGS_HOSTED=1", f"-I{inc}", c],
                            cwd=c.parent, check=False)
                    warn += r.stderr.count("warning:")
                so = run([py, "-c", "import zugodiff._core as c; print(c.__file__)"]).stdout.strip()
                nm = run(["nm", "-gU", so] if sys.platform == "darwin" else
                         ["nm", "-D", "--defined-only", so]).stdout
                exported = sorted({l.split()[-1].lstrip("_") for l in nm.splitlines() if l.strip()}
                                  - {"PyInit__core"})
                print(f"== {ref}: testing", file=sys.stderr)
                t = run([py, "-m", "pytest", "-q", "-p", "no:cacheprovider"], cwd=src, check=False)
                m = re.search(r"(\d+) passed", t.stdout)
                f = re.search(r"(\d+) failed", t.stdout)
                vend = (gdir / "VENDORED").read_text()
                pin = re.search(r"goose_commit (\w+)", vend).group(1)[:8]
                builds[ref] = dict(py=py, pin=pin, own=lines(own), gen=lines(gen), warn=warn,
                                   exported=", ".join(exported) or "only PyInit__core",
                                   passed=int(m.group(1)) if m else 0,
                                   failed=int(f.group(1)) if f else 0)
            print("\n## Builds\n")
            print("| ref | Goose pin | first-party C lines | generated C lines | -Wall warnings "
                  "| exported | tests passed (failed) |")
            print("| --- | --- | --- | --- | --- | --- | --- |")
            for ref, b in builds.items():
                print(f"| {ref} | {b['pin']} | {b['own']} | {b['gen']} | {b['warn']} | "
                      f"{b['exported']} | {b['passed']} ({b['failed']}) |")
            bench = tmp / "bench.py"
            bench.write_text(BENCH)
            times = {ref: {w: [] for w in WHATS} for ref in builds}
            refs = list(builds)
            for k in range(opts.rounds):
                print(f"== timing, round {k + 1} of {opts.rounds}", file=sys.stderr)
                # The order rotates each round: a fixed order biased the later
                # builds by a few per cent. Two identical builds still differ by
                # up to ~6%, so treat smaller differences as noise.
                for ref in refs[k % len(refs):] + refs[:k % len(refs)]:
                    b = builds[ref]
                    for line in run([b["py"], bench], cwd=tmp).stdout.splitlines():
                        what, value = line.split()
                        times[ref][what].append(float(value))
            print(f"\n## Core timings: median of {opts.rounds} rounds, alternating builds\n")
            print("| ref | " + " | ".join(WHATS.values()) + " |")
            print("| --- |" + " --- |" * len(WHATS))
            base = None
            for ref in builds:
                m = {w: statistics.median(times[ref][w]) for w in WHATS}
                if base is None:
                    base = m
                    cells = [f"{m[w]:.1f}" for w in WHATS]
                else:
                    cells = [f"{m[w]:.1f} ({100 * (m[w] / base[w] - 1):+.1f}%)" for w in WHATS]
                print(f"| {ref} | " + " | ".join(cells) + " |")
        finally:
            for d in tmp.iterdir():
                if (d / ".git").exists():
                    run(["git", "-C", root, "worktree", "remove", "--force", d], check=False)


if __name__ == "__main__":
    main()
