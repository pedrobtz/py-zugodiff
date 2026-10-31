# zugodiff

Myers and patience diff for Python, with the core written in
[Goose](https://github.com/aardappel/goose) and compiled to C.

**An experiment.** zugodiff tests whether Goose-generated C can live inside
CPython as it does inside R. The diff core is
[zudiff](https://github.com/pedrobtz/zudiff)'s, unchanged: the same Goose
source and the same generated C. This repository adds an R-free runtime and
the CPython glue. It is POSIX only and not on PyPI.

```python
import zugodiff

d = zugodiff.diff("abcabba", "cbabac", minimal=True)
d.distance        # 5
d.runs[:2]        # [Run(op='delete', old_start=0, new_start=0, length=2),
                  #  Run(op='equal', old_start=2, new_start=0, length=1)]

zugodiff.diff(b"hello world", b"hello there")       # bytes, byte by byte
zugodiff.diff_lines(old_text, new_text, algorithm="patience")
```

`diff()` takes two sequences of hashable items, or two bytes-like objects.
Items are equal when they hash and compare equal, as in a `set`, so `1`,
`1.0` and `True` match. Myers' search has GNU diff's cost limit by default;
`minimal=True` removes it.

## How it is built

```text
src/zugodiff/__init__.py               items to int32 ids (a dict), unpacking
src/zugodiff/_core.c                   CPython glue: the only C using Python.h
───────────────────────────── plain C below ─────────────────────────────
src/zugodiff/_goose/zudiff_goose.{c,h} generated from zudiff's Goose source
src/zugodiff/_goose/goose_vendor.c     copied from Goose's runtime_impl.h
src/zugodiff/_goose/goose_version.h    the Goose commit and runtime hash
src/zugodiff/_goose/goose_py_runtime.c first-party runtime, from zudiff's
```

`tools/sync-zudiff.sh <zudiff-checkout>` copies the generated files from
zudiff and records their origin in `src/zugodiff/_goose/VENDORED`. No Goose
compiler is needed to build: `pip install .` needs only a C compiler.

## What the runtime does

Goose's stock runtime ends the process on any failure. zudiff replaced it
with one that reports failures as R errors; `goose_py_runtime.c` is that
runtime with the R parts removed:

- **Failures are exceptions.** A bounds check, an assert or a data stack
  overflow jumps back to `goose_py_run()`. The data stacks are rewound, and
  the glue raises `zugodiff.GooseError`.
- **Ctrl-C is CPython's.** The core polls every 1024 search steps, and
  the poll calls `PyErr_CheckSignals()`. A `KeyboardInterrupt`, or any
  exception a Python signal handler raises, fails the call cleanly. The
  runtime installs no `SIGINT` handler, unlike zudiff's.
- **Nested calls are refused.** A signal handler that calls back into
  zugodiff gets `GooseError`, because Goose's globals are per process.
- **Guard pages.** Data stacks are reserved with a `PROT_NONE` gap. A fault
  handler is installed only while Goose code runs, and faults outside the
  stacks go back to Python's handlers.
- **Hidden symbols.** Only `PyInit__core` is exported.

The tests cover each of these: `tests/test_runtime.py`.

## Performance

`python bench/bench.py`, on an Intel i5-8500B, macOS, Python 3.13:

| Case | zugodiff | difflib | speed-up |
| --- | --- | --- | --- |
| 10^5 lines, 1 % changed | 51.9 ms | 599.9 ms | 11.6x |
| same, patience | 54.1 ms | 597.9 ms | 11.0x |
| 10^5 bytes, 100 edits | 0.9 ms | 25 s | (difflib's worst case) |
| 10^7 bytes, 100 edits | 51.1 ms | not run | |

On the text case the Goose core takes 1.9 ms. The other 45 ms are spent
turning lines into ids with a Python `dict`, so that step is the next
thing to move into C, still with Python's own hashing and equality.

## Licence

zugodiff's own code is MIT (`LICENSE`). The Goose runtime pieces and the C
the Goose compiler writes are Apache-2.0 (`LICENSES/goose-LICENSE`); see
`COPYRIGHTS`.
