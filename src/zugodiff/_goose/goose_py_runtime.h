/* goose_py_runtime.h -- running Goose-generated C inside a Python process.
   Plain C: nothing here includes Python.h. */
#ifndef GOOSE_PY_RUNTIME_H
#define GOOSE_PY_RUNTIME_H

#include <stddef.h>

/* Runs body(data) with Goose's guard-page handler installed. Returns 0 on
   success. On a Goose runtime failure (bounds check, abort, data stack
   overflow) it restores every data stack to where it was before the call,
   copies the message into err and returns 1. It returns 2 when the failure
   was an interrupt the poll hook reported. A call made while another is
   running is refused (returns 1) rather than run over its globals. */
int goose_py_run(void (*body)(void *), void *data, char *err, size_t errlen);

/* The host's interrupt check, called from zudiff_check_interrupt(), which
   the Goose code calls every 1024 search steps. A nonzero result fails the
   call with "interrupted". NULL means never interrupted. */
void goose_py_set_poll(int (*poll)(void));

/* Bytes in use across all data stacks. Call outside goose_py_run(). */
size_t goose_py_stack_used(void);

/* Test hook: the n-th interrupt poll of the next run raises SIGINT first. */
void goose_py_test_raise_after(long n);

/* Test hook: the first byte of the first data stack's guard gap, or NULL
   before the runtime has started. A write there inside goose_py_run() is
   what a data stack overflow does. */
unsigned char *goose_py_test_gap(void);

#endif
