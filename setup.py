"""Builds zugodiff._core: the CPython glue, Goose's runtime (hosted) and
zudiff's generated C. No Goose compiler is needed: the C is vendored."""

import sys

from setuptools import Extension, setup

if sys.platform == "win32":
    sys.exit("zugodiff: Windows is not supported yet")

goose = "src/zugodiff/_goose/"
setup(
    ext_modules=[
        Extension(
            "zugodiff._core",
            sources=[
                "src/zugodiff/_core.c",
                goose + "goose_runtime.c",
                goose + "zudiff_goose.c",
            ],
            # Goose's runtime in a host process: every failure returns to
            # goose_host_call, and nothing ends Python or writes to its
            # streams.
            define_macros=[("GS_HOSTED", "1")],
            # The runtime's gs_* names are Goose's; hide them so they cannot
            # clash with another extension's copy. Only PyInit__core is
            # exported (PyMODINIT_FUNC marks it visible).
            extra_compile_args=["-fvisibility=hidden", "-O2"],
        )
    ],
)
