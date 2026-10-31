/* _core.c -- CPython glue for zudiff's diff core: the only C here that
   uses Python's API.

   Every call into Goose goes through goose_py_run(). A Goose runtime
   failure jumps back to goose_py_run(), which rewinds the data stacks, so
   the body functions below must not hold Python references or raise: the
   jump would skip the cleanup. Buffers are borrowed through the buffer
   protocol before the run and released after it, and exceptions are set
   only after the run returns. The one exception is PyErr_CheckSignals(),
   which the interrupt poll calls during the run: it may set an exception
   (KeyboardInterrupt), and the call then fails with goose_py_run()
   returning 2 and that exception still set. The GIL is held throughout. */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <string.h>

#include "_goose/goose_py_runtime.h"
#include "_goose/goose_version.h"
#include "_goose/zudiff_goose.h"

#define ERRLEN 512
#define STR_(x) #x
#define STR(x) STR_(x)

static PyObject *GooseError;

/* Raises the error for a failed run: the interrupt's own exception when
   the poll set one, GooseError with the runtime's message otherwise. */
static PyObject *run_failed(int failed, const char *err) {
  if (failed == 2 && PyErr_Occurred()) return NULL;
  PyErr_SetString(GooseError, err);
  return NULL;
}

static int poll_signals(void) { return PyErr_CheckSignals() != 0; }

/* --- diffs ---------------------------------------------------------------- */

typedef struct {
  int bytes;
  sl_i32 ia, ib;
  sl_u8 ba, bb;
  int32_t algorithm;
  int64_t max_cost, n;
  const uint8_t *out;
} diff_call;

static void diff_body(void *p) {
  diff_call *c = p;
  if (c->bytes)
    c->out = zudiff_diff_bytes(c->ba, c->bb, c->algorithm, c->max_cost, &c->n);
  else
    c->out = zudiff_diff_ids(c->ia, c->ib, c->algorithm, c->max_cost, &c->n);
}

/* A buffer of int32 ids ("i" or "l" of size 4) or of bytes (item size 1). */
static int get_buffer(PyObject *o, Py_buffer *view, int *bytes) {
  if (PyObject_GetBuffer(o, view, PyBUF_C_CONTIGUOUS | PyBUF_FORMAT) < 0) return -1;
  const char *f = view->format ? view->format : "B";
  if (*f == '@' || *f == '=' || *f == '<') f++;
  if (view->itemsize == 1 && (*f == 'B' || *f == 'b' || *f == 'c')) {
    *bytes = 1;
    return 0;
  }
  if (view->itemsize == 4 && (*f == 'i' || *f == 'l')) {
    *bytes = 0;
    return 0;
  }
  PyBuffer_Release(view);
  PyErr_Format(PyExc_TypeError, "expected a buffer of bytes or of int32, got format '%s'",
               view->format ? view->format : "B");
  return -1;
}

/* _core.diff(a, b, algorithm, max_cost) -> bytes, the packed edit script.
   a and b are both bytes-like or both int32 buffers, borrowed, not copied.
   The script is copied into a bytes object after the run, since Goose
   reuses its memory. max_cost -1 asks for the default limit. */
static PyObject *core_diff(PyObject *self, PyObject *args) {
  PyObject *oa, *ob;
  int algorithm;
  long long max_cost;
  (void)self;
  if (!PyArg_ParseTuple(args, "OOiL", &oa, &ob, &algorithm, &max_cost)) return NULL;

  Py_buffer va, vb;
  int bytes_a, bytes_b;
  if (get_buffer(oa, &va, &bytes_a) < 0) return NULL;
  if (get_buffer(ob, &vb, &bytes_b) < 0) {
    PyBuffer_Release(&va);
    return NULL;
  }
  if (bytes_a != bytes_b) {
    PyBuffer_Release(&va);
    PyBuffer_Release(&vb);
    PyErr_SetString(PyExc_TypeError, "a and b must both be bytes or both int32");
    return NULL;
  }
  Py_ssize_t na = va.len / va.itemsize, nb = vb.len / vb.itemsize;
  if (na + nb >= INT32_MAX) {
    PyBuffer_Release(&va);
    PyBuffer_Release(&vb);
    PyErr_SetString(PyExc_OverflowError, "inputs longer than 2^31 - 1 together");
    return NULL;
  }

  diff_call c;
  memset(&c, 0, sizeof c);
  c.bytes = bytes_a;
  if (c.bytes) {
    c.ba.data = va.buf;
    c.ba.len = (int64_t)na;
    c.bb.data = vb.buf;
    c.bb.len = (int64_t)nb;
  } else {
    c.ia.data = va.buf;
    c.ia.len = (int64_t)na;
    c.ib.data = vb.buf;
    c.ib.len = (int64_t)nb;
  }
  c.algorithm = (int32_t)algorithm;
  c.max_cost = max_cost < 0 ? -1 : (int64_t)max_cost;

  char err[ERRLEN];
  int failed = goose_py_run(diff_body, &c, err, sizeof err);
  PyObject *out = failed ? NULL : PyBytes_FromStringAndSize((const char *)c.out, (Py_ssize_t)c.n);
  PyBuffer_Release(&va);
  PyBuffer_Release(&vb);
  if (failed) return run_failed(failed, err);
  return out;
}

/* --- test hooks ----------------------------------------------------------- */

typedef struct {
  int64_t i, v;
} probe_index_call;

static void probe_index_body(void *p) {
  probe_index_call *c = p;
  c->v = zudiff_probe_index(c->i);
}

static PyObject *core_probe_index(PyObject *self, PyObject *arg) {
  (void)self;
  probe_index_call c = {PyLong_AsLongLong(arg), 0};
  if (c.i == -1 && PyErr_Occurred()) return NULL;
  char err[ERRLEN];
  int failed = goose_py_run(probe_index_body, &c, err, sizeof err);
  if (failed) return run_failed(failed, err);
  return PyLong_FromLongLong(c.v);
}

typedef struct {
  int64_t n;
  uint8_t fail;
  int64_t v;
} probe_stack_call;

static void probe_stack_body(void *p) {
  probe_stack_call *c = p;
  c->v = zudiff_probe_stack(c->n, c->fail);
}

static PyObject *core_probe_stack(PyObject *self, PyObject *args) {
  long long n;
  int fail;
  (void)self;
  if (!PyArg_ParseTuple(args, "Lp", &n, &fail)) return NULL;
  probe_stack_call c = {n, (uint8_t)fail, 0};
  char err[ERRLEN];
  int failed = goose_py_run(probe_stack_body, &c, err, sizeof err);
  if (failed) return run_failed(failed, err);
  return PyLong_FromLongLong(c.v);
}

static void touch_gap_body(void *p) {
  *(volatile unsigned char *)p = 1;
}

/* A write to the guard gap, as a data stack overflow makes: it must come
   back as GooseError, not end the process. */
static PyObject *core_test_touch_gap(PyObject *self, PyObject *unused) {
  (void)self;
  (void)unused;
  unsigned char *gap = goose_py_test_gap();
  if (!gap) return PyErr_Format(GooseError, "no data stack");
  char err[ERRLEN];
  int failed = goose_py_run(touch_gap_body, gap, err, sizeof err);
  if (failed) return run_failed(failed, err);
  Py_RETURN_NONE;
}

static PyObject *core_stack_used(PyObject *self, PyObject *unused) {
  (void)self;
  (void)unused;
  return PyLong_FromSize_t(goose_py_stack_used());
}

static PyObject *core_test_raise_after(PyObject *self, PyObject *arg) {
  (void)self;
  long n = PyLong_AsLong(arg);
  if (n == -1 && PyErr_Occurred()) return NULL;
  goose_py_test_raise_after(n);
  Py_RETURN_NONE;
}

/* --- information ---------------------------------------------------------- */

static PyObject *core_goose_info(PyObject *self, PyObject *unused) {
  (void)self;
  (void)unused;
  return Py_BuildValue("(ss)", ZUDIFF_GOOSE_COMMIT, STR(GS_RUNTIME_VERSION));
}

/* --- module ----------------------------------------------------------------- */

static void init_body(void *unused) {
  static char name[] = "zugodiff";
  static char *argv[] = {name, NULL};
  (void)unused;
  goose_init(1, argv);
}

static PyMethodDef core_methods[] = {
    {"diff", core_diff, METH_VARARGS, "Packed edit script of two buffers."},
    {"probe_index", core_probe_index, METH_O, "Test hook: a bounds check on demand."},
    {"probe_stack", core_probe_stack, METH_VARARGS, "Test hook: grow a data stack."},
    {"stack_used", core_stack_used, METH_NOARGS, "Bytes in use on the data stacks."},
    {"test_touch_gap", core_test_touch_gap, METH_NOARGS,
     "Test hook: write to a data stack's guard gap."},
    {"test_raise_after", core_test_raise_after, METH_O,
     "Test hook: the n-th interrupt poll of the next run raises SIGINT."},
    {"goose_info", core_goose_info, METH_NOARGS, "Goose commit and runtime hash."},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef core_module = {PyModuleDef_HEAD_INIT, "_core",
                                         "zudiff's diff core, written in Goose.", -1,
                                         core_methods};

PyMODINIT_FUNC PyInit__core(void) {
  /* Goose's runtime has one set of globals per process. */
  static int started;
  char err[ERRLEN];
  if (!started) {
    if (goose_py_run(init_body, NULL, err, sizeof err)) {
      PyErr_Format(PyExc_ImportError, "zugodiff: cannot start the Goose runtime: %s", err);
      return NULL;
    }
    goose_py_set_poll(poll_signals);
    started = 1;
  }
  PyObject *m = PyModule_Create(&core_module);
  if (!m) return NULL;
  GooseError = PyErr_NewException("zugodiff.GooseError", PyExc_RuntimeError, NULL);
  if (!GooseError || PyModule_AddObjectRef(m, "GooseError", GooseError) < 0) {
    Py_XDECREF(GooseError);
    Py_DECREF(m);
    return NULL;
  }
  return m;
}
