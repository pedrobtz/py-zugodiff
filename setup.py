"""Builds zugodiff._core: the CPython glue, the R-free runtime and zudiff's
generated C. No Goose compiler is needed: the C is vendored."""

import sys

from setuptools import Extension, setup

if sys.platform == "win32":
    sys.exit("zugodiff: Windows is not supported yet (the Goose runtime is POSIX only)")

goose = "src/zugodiff/_goose/"
setup(
    ext_modules=[
        Extension(
            "zugodiff._core",
            sources=[
                "src/zugodiff/_core.c",
                goose + "goose_py_runtime.c",
                goose + "goose_vendor.c",
                goose + "zudiff_goose.c",
            ],
            # The runtime's gs_* names are Goose's; hide them so they cannot
            # clash with another extension's copy. Only PyInit__core is
            # exported (PyMODINIT_FUNC marks it visible).
            extra_compile_args=["-fvisibility=hidden", "-O2"],
        )
    ],
)
