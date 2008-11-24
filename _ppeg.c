/* vim: set et: */
#include <Python.h>
/* Override stdio printing */
#define printf PySys_WriteStdout
#include "lpeg.c"
#undef printf

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

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    Instruction *prog;
    Py_ssize_t prog_len;
    /* Environment values for the pattern. In theory, this should never be
     * self-referential, but in practice we should probably handle cyclic GC
     * here
     */
    PyObject *env;
} Pattern;

static void
Pattern_dealloc(Pattern* self)
{
    PyMem_Del(self->prog);
    Py_XDECREF(self->env);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
Pattern_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Pattern *self;

    self = (Pattern *)type->tp_alloc(type, 0);
    self->prog = NULL;
    self->env = NULL;
    return (PyObject *)self;
}

static int
Pattern_init(Pattern *self, PyObject *args, PyObject *kwds)
{
    self->prog = PyMem_New(Instruction, 1);
    if (self->prog == NULL)
        return -1;
    setinst(self->prog, IEnd, 0);
    self->prog_len = 1;

    return 0;
}

static int
Pattern_traverse(Pattern *self, visitproc visit, void *arg)
{
    Py_VISIT(self->env);
    return 0;
}

static int
Pattern_clear(Pattern *self)
{
    Py_CLEAR(self->env);
    return 0;
}

static int
value2fenv (PyObject *obj, PyObject *val)
{
    Pattern *patt = (Pattern *)obj;
    Py_XDECREF(patt->env);
    patt->env = PyList_New(1);
    if (patt->env == NULL)
        return -1;
    Py_INCREF(val);
    PyList_SET_ITEM(patt->env, 0, val);
    return 0;
}

static int
getlabel(PyObject *obj, PyObject *val)
{
    if (val == NULL)
        return 0;
    return value2fenv(obj, val);
}

/*
** {======================================================
** Verifier
** =======================================================
*/

static Py_ssize_t getposition(PyObject *patt, PyObject *positions, int i)
{
    PyObject *env = ((Pattern*)patt)->env;
    PyObject *key = PyList_GetItem(env, i);
    PyObject *val;
    if (key == NULL)
        return -1;
    val = PyDict_GetItem(positions, key);
    if (val == NULL)
        return -1;
    return PyInt_AsSsize_t(val);
}

static int verify (PyObject *patt, Instruction *op, const Instruction *p,
                   Instruction *e, PyObject *positions) {
  static const char dummy[] = "";
  Stack back[MAXBACK];
  int backtop = 0;  /* point to first empty slot in back */
  while (p != e) {
    switch ((Opcode)p->i.code) {
      case IRet: {
        p = back[--backtop].p;
        continue;
      }
      case IChoice: {
        if (backtop >= MAXBACK)
	{
	  PyErr_SetString(PyExc_RuntimeError, "Too many pending calls/choices");
	  return -1;
	}
        back[backtop].p = dest(0, p);
        back[backtop++].s = dummy;
        p++;
        continue;
      }
      case ICall: {
        assert((p + 1)->i.code != IRet);  /* no tail call */
        if (backtop >= MAXBACK)
	{
	  PyErr_SetString(PyExc_RuntimeError, "Too many pending calls/choices");
	  return -1;
	}
        back[backtop].s = NULL;
        back[backtop++].p = p + 1;
        goto dojmp;
      }
      case IOpenCall: {
        int i;
        if (positions == NULL)  /* grammar still not fixed? */
          goto fail;  /* to be verified later */
        for (i = 0; i < backtop; i++) {
          if (back[i].s == NULL && back[i].p == p + 1) {
            PyErr_SetString(PyExc_RuntimeError, "Rule is left recursive");
            /* val2str(L, rule) */
            return -1;
          }
        }
        if (backtop >= MAXBACK)
	{
	  PyErr_SetString(PyExc_RuntimeError, "Too many pending calls/choices");
	  return -1;
	}
        back[backtop].s = NULL;
        back[backtop++].p = p + 1;
        p = op + getposition(patt, positions, p->i.offset);
        continue;
      }
      case IBackCommit:
      case ICommit: {
        assert(backtop > 0 && p->i.offset > 0);
        backtop--;
        goto dojmp;
      }
      case IPartialCommit: {
        assert(backtop > 0);
        if (p->i.offset > 0) goto dojmp;  /* forward jump */
        else {  /* loop will be detected when checking corresponding rule */
          assert(positions != NULL);
          backtop--;
          p++;  /* just go on now */
          continue;
        }
      }
      case IAny:
      case IChar:
      case ISet: {
        if (p->i.offset == 0) goto fail;
        /* else goto dojmp; go through */
      }
      case IJmp: 
      dojmp: {
        p += p->i.offset;
        continue;
      }
      case IFailTwice:  /* 'not' predicate */
        goto fail;  /* body could have failed; try to backtrack it */
      case IFail: {
        if (p > op && (p - 1)->i.code == IBackCommit) {  /* 'and' predicate? */
          p++;  /* pretend it succeeded and go ahead */
          continue;
        }
        /* else failed: go through */
      }
      fail: { /* pattern failed: try to backtrack */
        do {
          if (backtop-- == 0)
            return 1;  /* no more backtracking */
        } while (back[backtop].s == NULL);
        p = back[backtop].p;
        continue;
      }
      case ISpan:
      case IOpenCapture: case ICloseCapture:
      case IEmptyCapture: case IEmptyCaptureIdx:
      case IFullCapture: {
        p += sizei(p);
        continue;
      }
      case ICloseRunTime: {
        goto fail;  /* be liberal in this case */
      }
      case IFunc: {
        const char *r = (p+1)->f((p+2)->buff, dummy, dummy, dummy);
        if (r == NULL) goto fail;
        p += p->i.offset;
        continue;
      }
      case IEnd:  /* cannot happen (should stop before it) */
      default: assert(0); return 0;
    }
  }
  assert(backtop == 0);
  return 0;
}


static void checkrule (PyObject *patt, Instruction *op, int from, int to,
                       PyObject *positions) {
  int i;
  int lastopen = 0;  /* more recent OpenCall seen in the code */
  for (i = from; i < to; i += sizei(op + i)) {
    if (op[i].i.code == IPartialCommit && op[i].i.offset < 0) {  /* loop? */
      int start = dest(op, i);
      assert(op[start - 1].i.code == IChoice && dest(op, start - 1) == i + 1);
      if (start <= lastopen) {  /* loop does contain an open call? */
        if (!verify(patt, op, op + start, op + i, positions)) /* check body */
#if 0
          luaL_error(L, "possible infinite loop in %s", val2str(L, rule));
#else
	{
	  PyErr_SetString(PyExc_RuntimeError, "Possible infinite loop");
	  return;
	}
#endif
      }
    }
    else if (op[i].i.code == IOpenCall)
      lastopen = i;
  }
  assert(op[i - 1].i.code == IRet);
  verify(patt, op, op + from, op + to - 1, positions);
}




/* }====================================================== */
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
    result->prog_len = n + 1;

    if (prog)
        *prog = result->prog;
    return (PyObject *)result;
}

static PyObject *
Pattern_display(Pattern* self)
{
    printpatt(self->prog);
    Py_RETURN_NONE;
}

static PyObject *
Pattern_dump(Pattern *self)
{
    PyObject *result = PyList_New(0);
    Instruction *p = self->prog;
    static const char *const names[] = {
        "any", "char", "set", "span", "ret", "end", "choice", "jmp", "call",
        "open_call", "commit", "partial_commit", "back_commit", "failtwice",
        "fail", "giveup", "func", "fullcapture", "emptycapture",
        "emptycaptureidx", "opencapture", "closecapture", "closeruntime"
    };

    if (result == NULL)
        return NULL;

    for (;;) {
        PyObject *item = Py_BuildValue("(sii)", names[p->i.code],
                p->i.aux, p->i.offset);
        if (item == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        if (PyList_Append(result, item) == -1) {
            Py_DECREF(item);
            Py_DECREF(result);
            return NULL;
        }
        if (p->i.code == IEnd) break;
        p += sizei(p);
    }

    return result;
}

static PyObject *newcharset (PyObject *cls, Instruction **prog)
{
    Instruction *p;
    PyObject *result = new_pattern(cls, CHARSETINSTSIZE, &p);
    if (result) {
        p[0].i.code = ISet;
        p[0].i.offset = 0;
        loopset(i, p[1].buff[i] = 0);
        if (prog) *prog = p;
    }
    return result;
}

static PyObject *
Pattern_Set(PyObject *cls, PyObject *arg)
{
    char *str;
    Py_ssize_t len;
    PyObject *result;
    Instruction *p;

    if (PyString_AsStringAndSize(arg, &str, &len) == -1)
        return NULL;

    if (len == 1) {
        /* a unit set is equivalent to a literal */
        result = new_pattern(cls, len, &p);
        if (result)
            setinstaux(p, IChar, 0, (byte)(*str));
    } else {
        result = newcharset(cls, &p);
        if (result) {
            while (len--) {
                setchar(p[1].buff, (byte)(*str));
                str++;
            }
        }
    }
    return result;
}

static PyObject *
Pattern_Range(PyObject *cls, PyObject *arg)
{
    char *str;
    Py_ssize_t len;
    PyObject *result;
    Instruction *p;

    if (PyString_AsStringAndSize(arg, &str, &len) == -1)
        return NULL;
    if (len % 2) {
        /* Argument must be a string of even length */
        PyErr_SetString(PyExc_ValueError, "Range argument must be a string of even length");
        return NULL;
    }

    result = newcharset(cls, &p);
    if (result == NULL)
        return NULL;

    for (; len > 0; len -= 2, str += 2) {
        int c;
        for (c = (byte)str[0]; c <= (byte)str[1]; c++)
            setchar(p[1].buff, c);
    }
    return result;
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
        if (prog) *prog = p;
    }
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

Py_ssize_t
jointables (Pattern *p1, Pattern *p2)
{
    Py_ssize_t n;

    if (p1->env == NULL) {
        n = 0;
        if (p2->env != NULL) {
            p1->env = p2->env;
            Py_INCREF(p1->env);
        }
    } else {
        n = PyList_Size(p1->env);
        if (p2->env != NULL) {
            /* Lists do support inplace concat */
            PyObject *new = PySequence_InPlaceConcat(p1->env, p2->env);
            if (new == NULL) {
                /* Error - what do we do??!? */
            }
            Py_XDECREF(new);
        }
    }
    return n;
}

#define pattsize(p) ((((Pattern*)(p))->prog_len) - 1)

Py_ssize_t
addpatt (Pattern *p1, Instruction *p, Pattern *p2)
{
    Py_ssize_t sz = pattsize(p2);
    Py_ssize_t corr = jointables(p1, p2);
    copypatt(p, p2->prog, sz + 1);
    if (corr != 0) {
        Instruction *px;
        for (px = p; px < p + sz; px += sizei(px)) {
            if (isfenvoff(px) && px->i.offset != 0)
                px->i.offset += corr;
        }
    }
    return sz;
}

PyObject *
Pattern_concat (PyObject *self, PyObject *other)
{
    /* TODO: Saner casting */
    Instruction *p1 = ((Pattern *)(self))->prog;
    Instruction *p2 = ((Pattern *)(other))->prog;

    if (isfail(p1) || issucc(p2)) {
        /* fail * x == fail; x * true == x */
        Py_INCREF(self);
        return self;
    }

    if (isfail(p2) || issucc(p1)) {
        /* true * x == x; x * fail == fail */
        Py_INCREF(other);
        return other;
    }

    if (isany(p1) && isany(p2)) {
        PyObject *type = PyObject_Type(self);
        PyObject *result = any(type, p1->i.aux + p2->i.aux, 0, NULL, NULL);
        Py_DECREF(type);
        return result;
    }
    else
    {
        Py_ssize_t l1 = pattsize(self);
        Py_ssize_t l2 = pattsize(other);
        PyObject *type = PyObject_Type(self);
        Instruction *np;
        PyObject *result = new_pattern(type, l1 + l2, &np);
        Py_DECREF(type);
        if (result) {
            Instruction *p = np + addpatt((Pattern*)result, np, (Pattern*)self);
            addpatt((Pattern*)result, p, (Pattern*)other);
            optimizecaptures(np);
        }
        return result;
    }
}

PyObject *
Pattern_and (PyObject *self)
{
    /* TODO: Saner casting */
    Instruction *p1 = ((Pattern *)(self))->prog;
    CharsetTag st1;
    Instruction *p;
    PyObject *type;
    PyObject *result;

    if (isfail(p1) || issucc(p1)) {
        /* &fail == fail; &true == true */
        Py_INCREF(self);
        return self;
    }

    type = PyObject_Type(self);

    if (tocharset(p1, &st1) == ISCHARSET) {
        result = new_pattern(type, CHARSETINSTSIZE + 1, &p);
        if (result) {
            setinst(p, ISet, CHARSETINSTSIZE + 1);
            loopset(i, p[1].buff[i] = ~st1.cs[i]);
            setinst(p + CHARSETINSTSIZE, IFail, 0);
        }
    }
    else {
        Py_ssize_t l1 = pattsize(self);
        result = new_pattern(type, 1 + l1 + 2, &p);
        if (result) {
            setinst(p++, IChoice, 1 + l1 + 1);
            p += addpatt((Pattern*)result, p, (Pattern*)self);
            setinst(p++, IBackCommit, 2);
            setinst(p, IFail, 0);
        }
    }

    Py_DECREF(type);
    return result;
}

static PyObject *
Pattern_diff (PyObject *self, PyObject *other)
{
    /* TODO: Saner casting */
    Instruction *p1 = ((Pattern *)(self))->prog;
    Instruction *p2 = ((Pattern *)(other))->prog;
    Py_ssize_t l1 = pattsize(self);
    Py_ssize_t l2 = pattsize(other);
    PyObject *type = PyObject_Type(self);
    PyObject *result;
    CharsetTag st1, st2;
    Instruction *p;

    if (tocharset(p1, &st1) == ISCHARSET && tocharset(p2, &st2) == ISCHARSET) {
        result = newcharset(type, &p);
        if (result)
            loopset(i, p[1].buff[i] = st1.cs[i] & ~st2.cs[i]);
    }
    else if (isheadfail(p2)) {
        result = new_pattern(type, l2 + 1 + l1, &p);
        if (result) {
            p += addpatt((Pattern*)result, p, (Pattern*)other);
            check2test(p - l2, l2 + 1);
            setinst(p++, IFail, 0);
            addpatt((Pattern*)result, p, (Pattern*)self);
        }
    }
    else {  /* !e2 . e1 */
        /* !e -> choice L1; e; failtwice; L1: ... */
        Instruction *pi;
        result = new_pattern(type, 1 + l2 + 1 + l1, &p);
        if (result) {
            pi = p;
            setinst(p++, IChoice, 1 + l2 + 1);
            p += addpatt((Pattern*)result, p, (Pattern*)other);
            setinst(p++, IFailTwice, 0);
            addpatt((Pattern*)result, p, (Pattern*)self);
            optimizechoice(pi);
        }
    }

    Py_DECREF(type);
    return result;
}


static PyObject *
Pattern_negate (PyObject *self)
{
    Instruction *p = ((Pattern*)(self))->prog;
    PyObject *type = PyObject_Type(self);
    PyObject *result;

    if (isfail(p)) {  /* -false? */
        result = PyObject_CallFunction(type, ""); /* true */
    }
    else if (issucc(p)) {  /* -true? */
        Instruction *p1;
        result = new_pattern(type, 1, &p1);  /* false */
        if (result)
            setinst(p1, IFail, 0);
    }
    else {  /* -A == '' - A */
        result = PyObject_CallFunction(type, "");
        if (result)
            result = Pattern_diff(result, self);
    }

    Py_DECREF(type);
    return result;
}

static PyObject *
repeatcharset (PyObject *cls, Charset cs, int l1, int n, PyObject *patt)
{
    /* e; ...; e; span; */
    int i;
    Instruction *p;
    PyObject *result = new_pattern(cls, n*l1 + CHARSETINSTSIZE, &p);
    if (result == NULL)
        return NULL;
    for (i = 0; i < n; i++) {
        p += addpatt((Pattern*)result, p, (Pattern*)patt);
    }
    setinst(p, ISpan, 0);
    loopset(k, p[1].buff[k] = cs[k]);
    return result;
}

static PyObject *
repeatheadfail (PyObject *cls, int l1, int n, PyObject *patt, Instruction **op)
{
    /* e; ...; e; L2: e'(L1); jump L2; L1: ... */
    int i;
    Instruction *p;
    PyObject *result;

    result = new_pattern(cls, (n + 1)*l1 + 1, &p);
    if (result == NULL)
        return NULL;
    if (op) *op = p;
    for (i = 0; i < n; i++) {
        p += addpatt((Pattern *)result, p, (Pattern *)patt);
    }
    p += addpatt((Pattern *)result, p, (Pattern *)patt);
    check2test(p - l1, l1 + 1);
    setinst(p, IJmp, -l1);
    return result;
}

static PyObject *
repeats (PyObject *cls, Instruction *p1, int l1, int n, PyObject *patt, Instruction **op)
{
  /* e; ...; e; choice L1; L2: e; partialcommit L2; L1: ... */
    int i;
    Instruction *p;
    PyObject *result;
    
    result = new_pattern(cls, (n + 1)*l1 + 2, &p);
    if (result == NULL)
        return NULL;

    /* Note - in verifier, there is a commented-out piece of code relating to
     * fenv lookups which needs fixing. This appears to be only relevant for
     * grammars.
     * TODO: Fix this when implementing grammars.
     */
    if (!verify(result, p1, p1, p1 + l1, NULL)) {
        PyErr_SetString(PyExc_ValueError, "Loop body may accept empty string");
        Py_DECREF(result);
        return NULL;
    }

    if (op) *op = p;
    for (i = 0; i < n; i++) {
        p += addpatt((Pattern*)result, p, (Pattern*)patt);
    }
    setinst(p++, IChoice, 1 + l1 + 1);
    p += addpatt((Pattern*)result, p, (Pattern*)patt);
    setinst(p, IPartialCommit, -l1);
    return result;
}

static PyObject *
optionalheadfail (PyObject *cls, int l1, int n, PyObject *patt)
{
    Instruction *p;
    PyObject *result = new_pattern(cls, n * l1, &p);
    int i;
    if (result == NULL)
        return NULL;
    for (i = 0; i < n; i++) {
        p += addpatt((Pattern *)result, p, (Pattern *)patt);
        check2test(p - l1, (n - i)*l1);
    }
    return result;
}

static PyObject *
optionals (PyObject *cls, int l1, int n, PyObject *patt)
{
    /* choice L1; e; partialcommit L2; L2: ... e; L1: commit L3; L3: ... */
    int i;
    Instruction *p;
    Instruction *op;
    PyObject *result = new_pattern(cls, n*(l1 + 1) + 1, &p);
    if (result == NULL)
        return NULL;
    op = p;
    setinst(p++, IChoice, 1 + n*(l1 + 1));
    for (i = 0; i < n; i++) {
        p += addpatt((Pattern*)result, p, (Pattern*)patt);
        setinst(p++, IPartialCommit, 1);
    }
    setinst(p - 1, ICommit, 1);  /* correct last commit */
    optimizechoice(op);
    return result;
}

static PyObject *
Pattern_pow (PyObject *self, PyObject *other, PyObject *modulo)
{
    int l1;
    long n = PyInt_AsLong(other);
    PyObject *type;
    PyObject *result;
    Instruction *p1;

    /* Ignore modulo argument - not meaningful */

    if (n == -1 && PyErr_Occurred())
        return NULL;

    type = PyObject_Type(self);
    p1 = ((Pattern*)(self))->prog;
    l1 = pattsize(self);
    if (n >= 0) {
        CharsetTag st;
        Instruction *op;
        if (tocharset(p1, &st) == ISCHARSET) {
            result = repeatcharset(type, st.cs, l1, n, self);
            goto ret;
        }
        if (isheadfail(p1))
            result = repeatheadfail(type, l1, n, self, &op);
        else
            result = repeats(type, p1, l1, n, self, &op);
        if (result) {
            optimizecaptures(op);
            optimizejumps(op);
        }
    }
    else {
        if (isheadfail(p1))
            result = optionalheadfail(type, l1, -n, self);
        else
            result = optionals(type, l1, -n, self);
    }
ret:
    Py_DECREF(type);
    return result;
}

static PyObject *
Pattern_Var (PyObject *cls, PyObject *name)
{
  Instruction *p;
  PyObject *result = new_pattern(cls, 1, &p);
  if (result == NULL)
      return NULL;
  setinst(p, IOpenCall, value2fenv(result, name));
  return result;
}

static PyObject *
Pattern_Grammar (PyObject *cls, PyObject *args, PyObject *kw)
{
    PyObject *result = NULL;
    PyObject *rules = NULL;
    PyObject *positions = NULL;
    int totalsize = 2; /* Include initial call and jump */
    int n = 0; /* Number of rules */
    Py_ssize_t i;
    Py_ssize_t nargs;
    Instruction *p;
    PyObject *init_rule = NULL;
    PyObject *initpos;
    Py_ssize_t pos;
    PyObject *k;
    PyObject *v;

    nargs = PySequence_Length(args);
    rules = PyList_New(0);
    positions = PyDict_New();

    if (nargs == -1 || rules == NULL || positions == NULL)
        goto err;

    /* Accumulate the list of rules and a mapping id->position */
    for (i = 0; i < nargs; ++i) {
        PyObject *patt = PySequence_GetItem(args, i);
        Py_ssize_t l;
        PyObject *py_ts;
        PyObject *py_i;
        if (!PyObject_IsInstance(patt, cls)) { /* TODO: Pattern, rather than cls? */
            if (i == 0) { /* initial rule */
                init_rule = patt;
                Py_INCREF(init_rule);
            }
            else {
                PyErr_SetString(PyExc_TypeError, "Grammar rule must be a pattern");
                Py_DECREF(patt);
                goto err;
            }
        }
        l = pattsize(patt) + 1; /* Space for pattern + RET */
        /* TODO: Error checking */
        py_ts = PyInt_FromSsize_t(totalsize);
        py_i = PyInt_FromSsize_t(i);
        if (py_ts == NULL || py_i == NULL) {
            Py_DECREF(patt);
            Py_XDECREF(py_ts);
            Py_XDECREF(py_i);
            goto err;
        }
        PyDict_SetItem(positions, py_i, py_ts);
        Py_DECREF(py_ts);
        Py_DECREF(py_i);
        PyList_Append(rules, patt);
        totalsize += l;
        ++n;
        Py_DECREF(patt);
    }
    pos = 0;
    while (kw && PyDict_Next(kw, &pos, &k, &v)) {
        PyObject *patt = v;
        Py_ssize_t l;
        PyObject *py_ts;
        if (!PyObject_IsInstance(patt, cls)) { /* TODO: Pattern, rather than cls? */
            PyErr_SetString(PyExc_TypeError, "Grammar rule must be a pattern");
            goto err;
        }
        l = pattsize(patt) + 1; /* Space for pattern + RET */
        /* TODO: Error checking */
        py_ts = PyInt_FromSsize_t(totalsize);
        if (py_ts == NULL) {
            goto err;
        }
        PyDict_SetItem(positions, k, py_ts);
        Py_DECREF(py_ts);
        PyList_Append(rules, patt);
        totalsize += l;
        ++n;
    }

    if (n == 0) {
        PyErr_SetString(PyExc_ValueError, "Empty grammar");
        goto err;
    }

    result = new_pattern(cls, totalsize, &p);
    if (result == NULL)
        goto err;
    nargs = PySequence_Length(rules);
    if (nargs == -1)
        goto err;
    ++p; /* Leave space for call */
    setinst(p++, IJmp, totalsize - 1);  /* after call, jumps to the end */

    for (i = 0; i < nargs; ++i) {
        PyObject *patt = PySequence_GetItem(rules, i);
        if (patt == NULL)
            goto err;
        p += addpatt((Pattern*)result, p, (Pattern*)patt);
        setinst(p++, IRet, 0);
    }
    p -= totalsize;  /* back to first position */
    totalsize = 2;  /* go through each rule's position */
    for (i = 0; i < nargs; i++) {  /* check all rules */
        PyObject *patt = PySequence_GetItem(rules, i);
        Py_ssize_t l;
        if (patt == NULL)
            goto err;
        l = pattsize(patt) + 1;
        /* Rule is only needed for error message */
#if 0
        checkrule(patt, p, totalsize, totalsize + l, positions);
#endif
        totalsize += l;
    }

    /* Get the initial rule */
    if (init_rule == NULL)
        init_rule = PyInt_FromLong(0);
    initpos = PyDict_GetItem(positions, init_rule);
    if (initpos == NULL) {
        PyErr_SetString(PyExc_ValueError, "Initial rule is not defined in the grammar");
        goto err;
    }
    pos = PyInt_AsSsize_t(initpos);
    setinst(p, ICall, pos);  /* first instruction calls initial rule */

    /* correct calls */
    for (i = 0; i < totalsize; i += sizei(p + i)) {
        if (p[i].i.code == IOpenCall) {
            /* TODO - definitely wrong (result isn't the right arg) */
            int pos = getposition(result, positions, p[i].i.offset);
            if (pos == -1 && PyErr_Occurred())
                goto err;
            p[i].i.code = (p[target(p, i + 1)].i.code == IRet) ? IJmp : ICall;
            p[i].i.offset = pos - i;
        }
    }
    optimizejumps(p);

    Py_DECREF(init_rule);
    Py_DECREF(rules);
    Py_DECREF(positions);
    return result;

err:
    Py_XDECREF(init_rule);
    Py_XDECREF(positions);
    Py_XDECREF(rules);
    Py_XDECREF(result);
    return NULL;
}

static PyObject *
capture_aux (PyObject *cls, PyObject *pat, int kind, PyObject *label)
{
    Py_ssize_t l1;
    int n;
    Instruction *p1;
    int lc;
    PyObject *result = NULL;

    p1 = ((Pattern *)(pat))->prog;
    l1 = pattsize(pat);
    lc = skipchecks(p1, 0, &n);

    if (lc == l1) {  /* got whole pattern? */
        /* may use a IFullCapture instruction at its end */
        Instruction *p;
        int labelid = getlabel(result, label);
        if (labelid == -1)
            return NULL;
        result = new_pattern(cls, l1 + 1, &p);
        if (result) {
            p += addpatt((Pattern*)result, p, (Pattern*)pat);
            setinstcap(p, IFullCapture, labelid, kind, n);
        }
    }
    else {  /* must use open-close pair */
        Instruction *op;
        Instruction *p;
        int labelid = getlabel(result, label);
        if (labelid == -1)
            return NULL;
        result = new_pattern(cls, 1 + l1 + 1, &op);
        if (result) {
            p = op;
            setinstcap(p++, IOpenCapture, labelid, kind, 0);
            p += addpatt((Pattern*)result, p, (Pattern*)pat);
            setinstcap(p, ICloseCapture, 0, Cclose, 0);
            optimizecaptures(op);
        }
    }

    return result;
}

static PyObject *Pattern_Capture(PyObject *cls, PyObject *pat) {
    return capture_aux(cls, pat, Csimple, 0);
}
static PyObject *Pattern_CaptureTab(PyObject *cls, PyObject *pat) {
    return capture_aux(cls, pat, Ctable, 0);
}
static PyObject *Pattern_CaptureSub(PyObject *cls, PyObject *pat) {
    return capture_aux(cls, pat, Csubst, 0);
}

static PyObject *Pattern_CapturePos(PyObject *cls) {
    Instruction *p;
    PyObject *result = new_pattern(cls, 1, &p);
    if (result)
        setinstcap(p, IEmptyCapture, 0, Cposition, 0);
    return result;
}

static PyObject *Pattern_CaptureArg(PyObject *cls, PyObject *id) {
    Instruction *p;
    long n = PyInt_AsLong(id);
    PyObject *result;
    if (n == -1 && PyErr_Occurred())
        return NULL;
    if (n <= 0 || n > SHRT_MAX) {
        PyErr_SetString(PyExc_ValueError, "Argument ID out of range");
        return NULL;
    }
    result = new_pattern(cls, 1, &p);
    if (result)
        setinstcap(p, IEmptyCapture, n, Carg, 0);
    return result;
}

static PyObject *Pattern_CaptureConst(PyObject *cls, PyObject *val) {
    Instruction *p;
    PyObject *result;
    result = new_pattern(cls, 1, &p);
    if (result) {
        setinstcap(p, IEmptyCapture, 0, Cconst, 0);
        ((Pattern*)result)->env = PyList_New(1);
        if (((Pattern*)result)->env == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        Py_INCREF(val);
        PyList_SET_ITEM(((Pattern*)result)->env, 0, val);
    }
    return result;
}

typedef struct CapState {
  Capture *cap;  /* current capture */
  Capture *ocap;  /* (original) capture list */
  PyObject *values; /* List of captured values */
  PyObject *args; /* args of match call */
  PyObject *env; /* env of pattern */
  int ptop;  /* index of last argument to 'match' */
  const char *s;  /* original string */
  int valuecached;  /* value stored in cache slot */
} CapState;

static int pushcapture (CapState *cs) {
    switch (captype(cs->cap)) {
        case Cposition: {
            long pos = cs->cap->s - cs->s;
            PyObject *val = PyInt_FromLong(pos);
            if (val == NULL)
                return 0;
            PyList_Append(cs->values, val);
            cs->cap++;
            return 1;
        }
        case Carg: {
            int arg = (cs->cap++)->idx;
            PyObject *val = PySequence_GetItem(cs->args, arg);
            if (val == NULL)
                return 0;
            PyList_Append(cs->values, val);
            return 1;
        }
        case Cconst: {
            int arg = (cs->cap++)->idx;
            PyObject *val = PySequence_GetItem(cs->env, arg);
            if (val == NULL)
                return 0;
            PyList_Append(cs->values, val);
            return 1;
        }
        default: {
            return 1;
        }
    }
}

static PyObject *getcaptures (PyObject *patt, Capture **capturep, const char *s, const char *r, PyObject *args)
{
    Capture *capture = *capturep;
    int n = 0;
    PyObject *result = PyList_New(0);
    if (result == NULL)
        return NULL;

    if (!isclosecap(capture)) { /* is there any capture? */
        CapState cs;
        cs.ocap = cs.cap = capture;
        cs.values = result;
        cs.s = s;
        cs.valuecached = 0;
        /* TODO: remove */
        cs.ptop = 0;
        cs.args = args;
        cs.env = ((Pattern*)patt)->env;
        do { /* collect the values */
            if (!pushcapture(&cs)) {
                Py_DECREF(result);
                return NULL;
            }
            n = 1; /* TODO: ???? */
        } while (!isclosecap(cs.cap));
    }
    if (n == 0) { /* No capture values */
        PyObject *val = PyInt_FromLong(r - s);
        if (!val) {
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(result);
        result = val;
        /* PyList_Append(result, val); */
    }

    return result;
}

static PyObject *
Pattern_call(Pattern* self, PyObject *args, PyObject *kw)
{
    char *str;
    Py_ssize_t len;
    Capture *cc = malloc(IMAXCAPTURES * sizeof(Capture));
    const char *e;

#if 0
    if (!PyArg_ParseTuple(args, "s#", &str, &len))
        return NULL;
#else
    PyObject *target = PyTuple_GetItem(args, 0);
    if (target == NULL || PyString_AsStringAndSize(target, &str, &len) == -1)
        return NULL;
#endif

    e = match("", str, str + len, self->prog, cc, 0);
    if (e == 0)
        Py_RETURN_NONE;
    return getcaptures((PyObject*)self, &cc, str, e, args);
}

static PyMethodDef Pattern_methods[] = {
    {"dump", (PyCFunction)Pattern_dump, METH_NOARGS,
     "Build a list representing the pattern, for debugging"
    },
    {"display", (PyCFunction)Pattern_display, METH_NOARGS,
     "Print the pattern, for debugging"
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
    {"Set", (PyCFunction)Pattern_Set, METH_O | METH_CLASS,
     "A pattern which matches a set of character(s)"
    },
    {"Range", (PyCFunction)Pattern_Range, METH_O | METH_CLASS,
     "A pattern which matches a set of character(s) given as ranges"
    },
    {"Cap", (PyCFunction)Pattern_Capture, METH_O | METH_CLASS,
     "A simple capture"
    },
    {"CapT", (PyCFunction)Pattern_CaptureTab, METH_O | METH_CLASS,
     "A table capture"
    },
    {"CapS", (PyCFunction)Pattern_CaptureSub, METH_O | METH_CLASS,
     "A substitution capture"
    },
    {"CapP", (PyCFunction)Pattern_CapturePos, METH_NOARGS | METH_CLASS,
     "A position capture"
    },
    {"CapA", (PyCFunction)Pattern_CaptureArg, METH_O | METH_CLASS,
     "An argument capture"
    },
    {"CapC", (PyCFunction)Pattern_CaptureConst, METH_O | METH_CLASS,
     "A constant capture"
    },
    {"Var", (PyCFunction)Pattern_Var, METH_O | METH_CLASS,
     "A grammar variable reference"
    },
    {"Grammar", (PyCFunction)Pattern_Grammar,
     METH_VARARGS | METH_KEYWORDS | METH_CLASS,
     "A grammar"
    },
    {"Dummy", (PyCFunction)Pattern_Dummy, METH_NOARGS | METH_CLASS,
     "A static value for testing"
    },
    {NULL}  /* Sentinel */
};

static PyNumberMethods Pattern_as_number = {
    (binaryfunc)Pattern_concat, /* binaryfunc nb_add */
    (binaryfunc)Pattern_diff,   /* binaryfunc nb_subtract */
    0, /* binaryfunc nb_multiply */
    0, /* binaryfunc nb_divide */
    0, /* binaryfunc nb_remainder */
    0, /* binaryfunc nb_divmod */
    (ternaryfunc)Pattern_pow, /* ternaryfunc nb_power */
    (unaryfunc)Pattern_negate, /* unaryfunc nb_negative */
    (unaryfunc)Pattern_and, /* unaryfunc nb_positive */
    0, /* unaryfunc nb_absolute */
    0, /* inquiry nb_nonzero */
    0, /* unaryfunc nb_invert */
    0, /* binaryfunc nb_lshift */
    0, /* binaryfunc nb_rshift */
    0, /* binaryfunc nb_and */
    0, /* binaryfunc nb_xor */
    0, /* binaryfunc nb_or */
    0, /* coercion nb_coerce */
    0, /* unaryfunc nb_int */
    0, /* unaryfunc nb_long */
    0, /* unaryfunc nb_float */
    0, /* unaryfunc nb_oct */
    0, /* unaryfunc nb_hex */
    0, /* binaryfunc nb_inplace_add */
    0, /* binaryfunc nb_inplace_subtract */
    0, /* binaryfunc nb_inplace_multiply */
    0, /* binaryfunc nb_inplace_divide */
    0, /* binaryfunc nb_inplace_remainder */
    0, /* ternaryfunc nb_inplace_power */
    0, /* binaryfunc nb_inplace_lshift */
    0, /* binaryfunc nb_inplace_rshift */
    0, /* binaryfunc nb_inplace_and */
    0, /* binaryfunc nb_inplace_xor */
    0, /* binaryfunc nb_inplace_or */
    0, /* binaryfunc nb_floor_divide */
    0, /* binaryfunc nb_true_divide */
    0, /* binaryfunc nb_inplace_floor_divide */
    0, /* binaryfunc nb_inplace_true_divide */
    0, /* unaryfunc nb_index */
};

static PyTypeObject PatternType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "_ppeg.Pattern",           /*tp_name*/
    sizeof(Pattern),           /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Pattern_dealloc,
                               /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    &Pattern_as_number,        /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    (ternaryfunc)Pattern_call, /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    /* Checktypes flag to allow patt ** int - thanks to Thomas Heller */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_CHECKTYPES,
                               /*tp_flags*/
    "Pattern object",          /* tp_doc */
    (traverseproc)Pattern_traverse,
                               /* tp_traverse */
    (inquiry)Pattern_clear,    /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Pattern_methods,           /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Pattern_init,    /* tp_init */
    0,                         /* tp_alloc */
    Pattern_new,               /* tp_new */
};

static PyMethodDef _ppeg_methods[] = {
    {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
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
