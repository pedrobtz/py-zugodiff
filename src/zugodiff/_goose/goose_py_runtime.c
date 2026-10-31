/* goose_py_runtime.c -- the part of the Goose runtime that zudiff's
   generated C needs, rewritten to live inside a Python process. First-party
   code; it replaces goose's src/runtime/runtime_impl.h, which is not
   vendored.

   Ported from zudiff (https://github.com/pedrobtz/zudiff), file
   src/goose_r_runtime.c at commit ecada10, POSIX half only. Differences
   from that copy:
   - No R. A Goose failure outside goose_py_run() is a bug in the glue and
     aborts, since there is no host error to raise from plain C.
   - No SIGINT handler. CPython already owns SIGINT: its handler sets a
     flag, and the glue's poll hook (PyErr_CheckSignals()) acts on it from
     zudiff_check_interrupt(). A KeyboardInterrupt, or any exception a
     Python signal handler raises, fails the call with "interrupted", and
     the exception is already set when goose_py_run() returns.
   - A nested call is refused instead of run over the outer call's
     globals: the poll hook runs Python signal handlers, which may call
     back into the package.
   - POSIX only. zudiff's Windows half (VirtualAlloc, a vectored exception
     handler) is not ported.

   Differences from the stock runtime, as in zudiff:
   - Nothing ends the process. Every runtime failure jumps back to
     goose_py_run(), which rewinds the data stacks and hands the message to
     the caller, who raises an ordinary Python exception.
   - No stdout, stderr, setvbuf, sigaltstack or threads.
   - The guard-page fault handler is installed only while Goose code runs.
     Python's handlers are restored afterwards, and a fault outside the
     data stacks is passed back to them.

   Byte search and number formatting live in goose_vendor.c, copied
   verbatim from the stock runtime by zudiff's tools/regenerate.sh. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "goose_py_runtime.h"
#include "goose_version.h"

#define GS_API
#define GS_NORETURN __attribute__((noreturn))
#define GS_CAT_(a, b) a##b
#define GS_CAT(a, b) GS_CAT_(a, b)
#define gs_rt_start GS_CAT(gs_rt_start_, GS_RUNTIME_VERSION)

typedef struct gs_stack { uint8_t *top; } gs_stack;

/* The jump back to goose_py_run(). sigsetjmp restores the signal mask,
   which a signal handler leaves blocked. */
typedef sigjmp_buf gs_jmp;
#define GS_SETJMP(b) sigsetjmp(b, 1)
#define GS_LONGJMP(b) siglongjmp(b, 1)

/* --- data stack regions -------------------------------------------------- */

#define GS_PY_MAX_REGIONS 64

typedef struct {
    uint8_t *base;
    size_t size;      /* usable part plus the PROT_NONE guard gap */
    gs_stack *stack;  /* whose top goose_py_run saves and rewinds */
    uint8_t *saved;
} gs_py_region;

static gs_py_region regions[GS_PY_MAX_REGIONS];
static long nregions;
static size_t region_usable, region_gap;

/* --- failures: jump back to goose_py_run --------------------------------- */

static gs_jmp *active_jump;
static int running;
static char failure[512];

static int (*poll_hook)(void);
static int interrupt_failed;
static long raise_after;

static GS_NORETURN void gs_py_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(failure, sizeof failure, fmt, ap);
    va_end(ap);
    if (active_jump) GS_LONGJMP(*active_jump);
    /* Goose code ran outside goose_py_run: a bug in the glue, and there is
       no host error to raise from here. */
    abort();
}

GS_API GS_NORETURN void gs_panic(const char *msg) { gs_py_fail("%s", msg); }

static const char *const errmsgs[] = {
    "limited array capacity exceeded",
    "slice bounds out of range",
    "relative reference offset overflow",
    "assert failed",
    "invalid capacity",
    "resize growth requires a fill value",
    "resize to a negative length",
    "pop on empty array",
    "thread_wait on an unknown thread id",
    "a worker cannot wait for itself",
    "corrupt ADT tag",
    "serialization needs a little-endian host",
    "invalid slice length",
    "slice not from this pool",
    "non-null relative reference encodes as null",
    "pop_n count negative or beyond the array's length",
    "queue limit of zero or less",
};

GS_API GS_NORETURN void gs_abort(int err, const char *file, int line) {
    const char *m = err >= 0 && err < (int)(sizeof errmsgs / sizeof *errmsgs)
                        ? errmsgs[err] : "runtime error";
    gs_py_fail("%s (%s:%d)", m, file, line);
}

GS_API GS_NORETURN void gs_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line) {
    gs_py_fail("%.*s (%s:%d)", (int)(len > 400 ? 400 : len), (const char *)msg, file, line);
}

GS_API GS_NORETURN void gs_exit(int64_t code) {
    gs_py_fail("exit(%lld) called", (long long)code);
}

GS_API GS_NORETURN void gs_idxfail(int64_t i, int64_t n, const char *file, int line) {
    gs_py_fail("index %lld out of bounds (length %lld) (%s:%d)", (long long)i,
               (long long)n, file, line);
}

GS_API GS_NORETURN void gs_divfail(const char *file, int line) {
    gs_py_fail("division by zero (%s:%d)", file, line);
}

GS_API GS_NORETURN void gs_divovf(const char *file, int line) {
    gs_py_fail("division overflow (%s:%d)", file, line);
}

GS_API GS_NORETURN void gs_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line) {
    gs_py_fail("%s overflow: %lld %s %lld (%s:%d)", type, (long long)a, op, (long long)b,
               file, line);
}

GS_API GS_NORETURN void gs_ovf_neg(int64_t a, const char *type, const char *file, int line) {
    gs_py_fail("%s overflow: -%lld (%s:%d)", type, (long long)a, file, line);
}

GS_API GS_NORETURN void gs_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line) {
    gs_py_fail("%lld as %s: %s (%s:%d)", (long long)v, type, why, file, line);
}

GS_API GS_NORETURN void gs_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line) {
    gs_py_fail("%llu as %s: %s (%s:%d)", (unsigned long long)v, type, why, file, line);
}

GS_API GS_NORETURN void gs_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line) {
    (void)f32;
    gs_py_fail("%g as %s: %s (%s:%d)", d, type, why, file, line);
}

/* --- interrupts ---------------------------------------------------------- */

GS_API void zudiff_check_interrupt(void) {
    if (raise_after > 0 && --raise_after == 0) raise(SIGINT);
    if (poll_hook && poll_hook()) {
        interrupt_failed = 1;
        gs_py_fail("interrupted");
    }
}

void goose_py_set_poll(int (*poll)(void)) { poll_hook = poll; }

void goose_py_test_raise_after(long n) { raise_after = n; }

/* --- start-up and regions ------------------------------------------------ */

GS_API void gs_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions) {
    (void)argc; (void)argv; (void)budget; (void)workerregions;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    if (mainregions > GS_PY_MAX_REGIONS) gs_py_fail("too many data stacks");
    region_usable = (size_t)reserve - (size_t)reserve % page;
    region_gap = ((size_t)gap + page - 1) / page * page;
    if (!region_usable || !region_gap) gs_py_fail("bad data stack sizes");
}

/* Address space for one data stack: the usable part read-write, committed
   by the kernel as it is touched, then a PROT_NONE gap that turns runaway
   growth into a fault. */
GS_API uint8_t *gs_reserve_region(gs_stack *stack) {
    if (nregions >= GS_PY_MAX_REGIONS) gs_py_fail("too many data stack regions");
    size_t size = region_usable + region_gap;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED) gs_py_fail("cannot reserve data stack address space");
    if (mprotect((uint8_t *)p + region_usable, region_gap, PROT_NONE)) {
        munmap(p, size);
        gs_py_fail("cannot protect data stack guard gap");
    }
    regions[nregions].base = (uint8_t *)p;
    regions[nregions].size = size;
    regions[nregions].stack = stack;
    nregions++;
    return (uint8_t *)p;
}

/* --- running Goose code -------------------------------------------------- */

static struct sigaction prev_segv, prev_bus;

static void fault_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    uintptr_t hit = (uintptr_t)info->si_addr;
    for (long i = 0; i < nregions; i++) {
        if (hit - (uintptr_t)regions[i].base < regions[i].size && active_jump) {
            snprintf(failure, sizeof failure, "data stack overflow");
            GS_LONGJMP(*active_jump);
        }
    }
    /* Not ours: put Python's handler back; the access faults again under
       it. */
    sigaction(sig, sig == SIGSEGV ? &prev_segv : &prev_bus, NULL);
}

int goose_py_run(void (*body)(void *), void *data, char *err, size_t errlen) {
    gs_jmp jump;
    struct sigaction sa;
    long i;

    if (running) {
        if (err && errlen)
            snprintf(err, errlen, "Goose re-entered during a Goose call");
        return 1;
    }
    running = 1;
    for (i = 0; i < nregions; i++) regions[i].saved = regions[i].stack->top;

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &prev_segv);
#ifdef SIGBUS
    sigaction(SIGBUS, &sa, &prev_bus);
#endif
    interrupt_failed = 0;

    int failed = 0;
    if (GS_SETJMP(jump) == 0) {
        active_jump = &jump;
        body(data);
    } else {
        failed = interrupt_failed ? 2 : 1;
        /* Stacks that existed before the call go back to their old tops;
           regions made during it (goose_init) are left as they are. */
        for (i = 0; i < nregions; i++)
            if (regions[i].saved) regions[i].stack->top = regions[i].saved;
        if (err && errlen) snprintf(err, errlen, "%s", failure);
    }
    active_jump = NULL;
    raise_after = 0;

    sigaction(SIGSEGV, &prev_segv, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS, &prev_bus, NULL);
#endif
    running = 0;
    return failed;
}

/* Test hook: the first byte of the first data stack's guard gap, so that
   the fault path can be tested without filling a stack. */
unsigned char *goose_py_test_gap(void) {
    return nregions ? regions[0].base + region_usable : NULL;
}

/* Bytes in use across all data stacks, for the tests: after any call,
   failed or not, it must be back where it was before. */
size_t goose_py_stack_used(void) {
    size_t used = 0;
    for (long i = 0; i < nregions; i++)
        used += (size_t)(regions[i].stack->top - regions[i].base);
    return used;
}
