#include <Python.h>
#include "lpeg.c"

/* }====================================================== */

static const Instruction Dummy[] =
{
    {{ICall,0,2}},
    {{IJmp,0,11}},
    {{IChar,'O',7}},
    {{IChoice,1,6}},
    {{IChar,'m',0}},
    {{IChar,'e',0}},
    {{IChar,'g',0}},
    {{IChar,'a',0}},
    {{ICommit,0,3}},
    {{IAny,1,0}},
    {{IJmp,0,-8}},
    {{IRet,0,0}},
};

int domatch(Instruction *p, char *s) {
    Capture cc[IMAXCAPTURES];
    const char *e = match("", s, s+strlen(s), p, cc, 0);
    return e-s;
}

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    Instruction *prog;
    Py_ssize_t prog_len;
} Pattern;

static void
Pattern_dealloc(Pattern* self)
{
    PyMem_Del(self->prog);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
Pattern_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Pattern *self;

    self = (Pattern *)type->tp_alloc(type, 0);
    self->prog = NULL;
    return (PyObject *)self;
}

static int
Pattern_init(Pattern *self, PyObject *args, PyObject *kwds)
{
    self->prog = PyMem_New(Instruction, 1);
    if (self->prog == NULL)
	return -1;
    setinst(self->prog, IEnd, 0);

    return 0;
}

static PyObject *
new_pattern(PyObject *cls, Py_ssize_t n, Instruction **prog)
{
    Pattern *result = (Pattern *)PyObject_CallFunction(cls, "");
    if (result == NULL)
	return NULL;

    if (n >= MAXPATTSIZE - 1) {
	PyErr_SetString(PyExc_ValueError, "Pattern too big");
	return NULL;
    }
    result->prog = PyMem_Resize(result->prog, Instruction, n + 1);
    if (result->prog == NULL) {
	Py_DECREF(result);
	return NULL;
    }

    setinst(result->prog + n, IEnd, 0);

    if (prog)
	*prog = result->prog;
    return (PyObject *)result;
}

static PyObject *
Pattern_dump(Pattern* self)
{
    printpatt(self->prog);
    Py_RETURN_NONE;
}

static PyObject *any (PyObject *cls, int n, int extra, int *offsetp, Instruction **prog)
{
  int offset = offsetp ? *offsetp : 0;
  Instruction *p;
  PyObject *result = new_pattern(cls, (n - 1)/UCHAR_MAX + extra + 1, &p);
  Instruction *p1 = p + offset;
  if (result) {
      for (; n > UCHAR_MAX; n -= UCHAR_MAX)
	setinstaux(p1++, IAny, 0, UCHAR_MAX);
      setinstaux(p1++, IAny, 0, n);
      if (offsetp) *offsetp = p1 - p;
  }
  if (prog)
      *prog = p;
  return result;
}

static PyObject *
Pattern_Any(PyObject *cls, PyObject *arg)
{
    long n = PyInt_AsLong(arg);
    PyObject *result;

    if (n == -1 && PyErr_Occurred())
	return NULL;

    if (n == 0) {
	/* Match the null string */
	/* Nothing more to do */
	result = new_pattern(cls, 0, NULL);
    }
    else if (n > 0) {
	result = any(cls, n, 0, NULL, NULL);
    }
    else if (-n <= UCHAR_MAX) {
	Instruction *p;
	result = new_pattern(cls, 2, &p);
	if (result) {
	    setinstaux(p, IAny, 2, -n);
	    setinst(p + 1, IFail, 0);
	}
    }
    else {
        int offset = 2;  /* space for IAny & IChoice */
	Instruction *p;
        result = any(cls, -n - UCHAR_MAX, 3, &offset, &p);
	if (result) {
	    setinstaux(p, IAny, offset + 1, UCHAR_MAX);
	    setinstaux(p + 1, IChoice, offset, UCHAR_MAX);
	    setinst(p + offset, IFailTwice, 0);
	}
    }
    return result;
}

static PyObject *
Pattern_Match(PyObject *cls, PyObject *arg)
{
    char *str;
    Py_ssize_t len;
    Py_ssize_t i;
    PyObject *result;
    Instruction *p;

    if (PyString_AsStringAndSize(arg, &str, &len) == -1)
	return NULL;

    result = new_pattern(cls, len, &p);
    if (result == NULL)
	return NULL;
    for (i = 0; i < len; ++i)
	setinstaux(p+i, IChar, 0, (byte)str[i]);

    return result;
}

static PyObject *
Pattern_Fail(PyObject *cls)
{
    PyObject *result;
    Instruction *p;

    result = new_pattern(cls, 1, &p);
    if (result == NULL)
	return NULL;

    setinst(p, IFail, 0);
    return result;
}

static PyObject *
Pattern_Dummy(PyObject *cls)
{
    Instruction *p;
    PyObject *result = new_pattern(cls, sizeof(Dummy)/sizeof(*Dummy), &p);
    if (result)
	memcpy(p, Dummy, sizeof(Dummy));
    return result;
}

static PyObject *
Pattern_call(Pattern* self, PyObject *args, PyObject *kw)
{
    char *str;
    Py_ssize_t len;
    Capture cc[IMAXCAPTURES];
    const char *e;

    if (!PyArg_ParseTuple(args, "s#", &str, &len))
	return NULL;

    e = match("", str, str + len, self->prog, cc, 0);
    if (e == 0)
	Py_RETURN_NONE;
    return PyInt_FromLong(e - str);
}

static PyMethodDef Pattern_methods[] = {
    {"dump", (PyCFunction)Pattern_dump, METH_NOARGS,
     "Dump the pattern, for debugging"
    },
    {"Any", (PyCFunction)Pattern_Any, METH_O | METH_CLASS,
     "A pattern which matches any character(s)"
    },
    {"Match", (PyCFunction)Pattern_Match, METH_O | METH_CLASS,
     "A pattern which matches a specific string"
    },
    {"Fail", (PyCFunction)Pattern_Fail, METH_NOARGS | METH_CLASS,
     "A pattern which never matches"
    },
    {"Dummy", (PyCFunction)Pattern_Dummy, METH_NOARGS | METH_CLASS,
     "A static value for testing"
    },
    {NULL}  /* Sentinel */
};

static PyTypeObject PatternType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "_ppeg.Pattern",           /*tp_name*/
    sizeof(Pattern),           /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Pattern_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    (ternaryfunc)Pattern_call, /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Pattern object",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    Pattern_methods,           /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Pattern_init,                         /* tp_init */
    0,                         /* tp_alloc */
    Pattern_new,               /* tp_new */
};

static PyMethodDef _ppeg_methods[] = {
    {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
init_ppeg(void)
{
    PyObject* m;

    if (PyType_Ready(&PatternType) < 0)
        return;

    m = Py_InitModule3("_ppeg", _ppeg_methods, "PEG parser module.");
    if (m == NULL)
	return;

    Py_INCREF(&PatternType);
    PyModule_AddObject(m, "Pattern", (PyObject *)&PatternType);
}
