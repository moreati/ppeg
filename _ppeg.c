/* vim: set et: */
#include <Python.h>
#include <structmember.h>
/* Override stdio printing */
#define printf PySys_WriteStdout
#include "lpeg.c"
#undef printf

/*#define TRACE 1 */

#define DEBUG 1
#undef DEBUG
#ifdef DEBUG
#define D(s) (fprintf(stderr, s "\n"), fflush(stderr))
#define D1(s,a) (fprintf(stderr, s "\n",a), fflush(stderr))
#define D2(s,a,b) (fprintf(stderr, s "\n",a,b), fflush(stderr))
#define D3(s,a,b,c) (fprintf(stderr, s "\n",a,b,c), fflush(stderr))
#else
#define D(s)
#define D1(s,a)
#define D2(s,a,b)
#define D3(s,a,b,c)
#endif

static const char *const instruction_names[] = {
    "any", "char", "set", "span", "ret", "end", "choice", "jmp", "call",
    "open_call", "commit", "partial_commit", "back_commit", "failtwice",
    "fail", "giveup", "func", "fullcapture", "emptycapture",
    "emptycaptureidx", "opencapture", "closecapture", "closeruntime"
};

#define INAME(i) (instruction_names[i])

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

/* Forward declaration of the Pattern and Match types */
static PyTypeObject PatternType;
static PyTypeObject MatchType;
#define pattern_cls ((PyObject *)(&PatternType))
#define match_cls ((PyObject *)(&MatchType))

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
#ifdef TRACE
    PyObject *trace;
#endif
} Pattern;

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    long pos;
    PyObject *captures;
} Match;

/* Accessors - object must be of the correct type!
 * These are lvalues, and can be used as the target of an assignment.
 */
#define patprog(pat) (((Pattern *)(pat))->prog)
#define patlen(pat) (((Pattern *)(pat))->prog_len)
#define patenv(pat) (((Pattern *)(pat))->env)
#define patsize(pat) ((patlen(pat)) - 1)

/* **********************************************************************
 * Pattern object creation
 * **********************************************************************
 */
/* Reset the instruction buffer for the given object */
static int resize_patt(PyObject *patt, Py_ssize_t n) {
    Instruction *p;

    if (n >= MAXPATTSIZE - 1) {
        PyErr_SetString(PyExc_ValueError, "Pattern too big");
        return -1;
    }

    PyMem_Del(patprog(patt));
    patprog(patt) = p = PyMem_New(Instruction, n + 1);
    if (p == NULL) {
        return -1;
    }

    memset(p, 0, (sizeof(Instruction) * n+1));
    setinst(p + n, IEnd, 0);
    patlen(patt) = n + 1;
    return 0;
}

/* Create a new pattern with the given class */
static PyObject *new_patt(PyObject *cls, Py_ssize_t n) {
    PyObject *result = PyObject_CallFunction(cls, "");
    if (result == NULL)
        return NULL;

    /* n == 0 means don't further initialise the buffer */
    if (n == 0)
        return result;

    if (resize_patt(result, n) == -1) {
        Py_DECREF(result);
        return NULL;
    }

    return result;
}

/* Create a new pattern with the same class as the given object */
#define empty_patt(source, n) (new_patt((PyObject*)(source->ob_type),(n)))

/* Clear a characterset pattern */
static void clear_charset(Instruction *p) {
    setinst(p, ISet, 0);
    loopset(i, p[1].buff[i] = 0);
}

/* Create a new characterset pattern */
static PyObject *new_charset(PyObject *cls)
{
    PyObject *result = new_patt(cls, CHARSETINSTSIZE);
    if (result)
        clear_charset(patprog(result));
    return result;
}

#define empty_charset(source) (new_charset((PyObject*)(source->ob_type)))

/* Make sure *self and *other are both patterns.
 * If both are, just incref them.
 * It's not possible for neither to be (we couldn't reach this code in that
 * case).
 * If only one is a pattern, make the other into one by calling the former's
 * constructor on the latter.
 * Always make *self and *other into new references.
 *
 * Return -1 on errors.
 */
int ensure_patterns(PyObject **self, PyObject **other) {
    /* Note - this check does NOT ensure that we cast to the "more derived"
     * type. Rather, mixed operations result in a value of the left-hand type.
     */
    int selfpatt = PyObject_IsInstance(*self, pattern_cls);
    int otherpatt = PyObject_IsInstance(*other, pattern_cls);

    if (selfpatt && otherpatt) {
        Py_INCREF(*self);
        Py_INCREF(*other);
    }
    else if (selfpatt) {
        PyObject *cls = (PyObject*)((*self)->ob_type);
        PyObject *result = PyObject_CallFunctionObjArgs(cls, *other, NULL);
        if (result == NULL)
            return -1;
        *other = result;
        Py_INCREF(*self);
    }
    else { /* other is a pattern */
        PyObject *cls = (PyObject*)((*other)->ob_type);
        PyObject *result = PyObject_CallFunctionObjArgs(cls, *self, NULL);
        if (result == NULL)
            return -1;
        *self = result;
        Py_INCREF(*other);
    }
    return 0;
}

/* Merge the environments of 2 patterns */
Py_ssize_t mergeenv (PyObject *p1, PyObject *p2) {
    PyObject *e1 = patenv(p1);
    PyObject *e2 = patenv(p2);
    Py_ssize_t n;

    if (e1 == NULL) {
        /* No correction needed */
        n = 0;
        if (e2 != NULL) {
            Py_INCREF(e2);
            patenv(p1) = e2;
        }
    } else {
        n = PyList_Size(e1);
        if (e2 != NULL) {
            PyObject *new = PySequence_InPlaceConcat(e1, e2);
            if (new == NULL)
                return -1;
            Py_XDECREF(new);
        }
    }
    return n;
}

/* Add the pattern other to the pattern self, starting at position p (which
 * must point inside the instruction list of self).
 * Return the number of instructions added.
 */
Py_ssize_t addpatt (PyObject *self, Instruction *p, PyObject *other)
{
    Py_ssize_t sz = patsize(other);
    Py_ssize_t corr;

    /* Merge the environments */
    corr = mergeenv(self, other);
    if (corr == -1)
        return -1;
    /* Copy the instructions */
    copypatt(p, patprog(other), sz + 1);
    /* Correct the offsets, if needed */
    if (corr != 0) {
        Instruction *px;
        for (px = p; px < p + sz; px += sizei(px)) {
            if (isfenvoff(px) && px->i.offset != 0)
                px->i.offset += corr;
        }
    }
    return sz;
}

/* **********************************************************************
 * Object administrative functions - memory management
 * **********************************************************************
 */
/* Pattern */
static void Pattern_dealloc(Pattern* self)
{
    PyMem_Del(self->prog);
    Py_XDECREF(self->env);
#ifdef TRACE
    Py_XDECREF(self->trace);
#endif
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *Pattern_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self = type->tp_alloc(type, 0);
    if (self) {
        patprog(self) = NULL;
        patenv(self) = NULL;
#ifdef TRACE
        ((Pattern*)self)->trace = NULL;
#endif
    }
    return self;
}

static int Pattern_traverse(Pattern *self, visitproc visit, void *arg) {
    Py_VISIT(self->env);
#ifdef TRACE
    Py_VISIT(self->trace);
#endif
    return 0;
}

static int Pattern_clear(Pattern *self) {
    Py_CLEAR(self->env);
#ifdef TRACE
    Py_CLEAR(self->trace);
#endif
    return 0;
}

/* Match */
static void Match_dealloc(Match* self)
{
    Py_XDECREF(self->captures);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *Match_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self = type->tp_alloc(type, 0);
    if (self) {
        ((Match*)self)->pos = -1;
        ((Match*)self)->captures = NULL;
    }
    return self;
}

static int Match_traverse(Match *self, visitproc visit, void *arg) {
    Py_VISIT(self->captures);
    return 0;
}

static int Match_clear(Match *self) {
    Py_CLEAR(self->captures);
    return 0;
}

static int Match_nonzero(Match *self) {
    return (self->pos != -1);
}

/* **********************************************************************
 * Object administrative functions - initialisation
 * **********************************************************************
 */
static int init_fail(PyObject *self) {
    if (resize_patt(self, 1) == -1)
        return -1;
    setinst(patprog(self), IFail, 0);
    return 0;
}

static int init_match(PyObject *self, char *str, Py_ssize_t len) {
    Py_ssize_t i;
    Instruction *p;
    if (resize_patt(self, len) == -1)
        return -1;
    p = patprog(self);
    for (i = 0; i < len; ++i)
        setinstaux(p+i, IChar, 0, (byte)str[i]);
    return 0;
}

static int init_set(PyObject *self, char *str, Py_ssize_t len) {
    if (len == 1) {
        /* a unit set is equivalent to a literal */
        if (resize_patt(self, len) == -1)
            return -1;
        setinstaux(patprog(self), IChar, 0, (byte)(*str));
    } else {
        Instruction *p;
        if (resize_patt(self, CHARSETINSTSIZE) == -1)
            return -1;
        p = patprog(self);
        clear_charset(p);
        while (len--) {
            setchar(p[1].buff, (byte)(*str));
            str++;
        }
    }
    return 0;
}

static int init_range(PyObject *self, char *str, Py_ssize_t len) {
    Instruction *p;
    if (len % 2) {
        /* Argument must be a string of even length */
        PyErr_SetString(PyExc_ValueError, "Range argument must be a string of even length");
        return -1;
    }
    if (resize_patt(self, CHARSETINSTSIZE) == -1)
        return -1;
    p = patprog(self);
    clear_charset(p);
    for (; len > 0; len -= 2, str += 2) {
        int c;
        for (c = (byte)str[0]; c <= (byte)str[1]; c++)
            setchar(p[1].buff, c);
    }
    return 0;
}

static Py_ssize_t fill_any(PyObject *self, Py_ssize_t n, int extra, int offset) {
    Instruction *p;
    Instruction *p1;
    if (resize_patt(self, (n - 1) / UCHAR_MAX + extra + 1) == -1)
        return -1;
    p = patprog(self);
    p1 = p + offset;
    for (; n > UCHAR_MAX; n -= UCHAR_MAX)
        setinstaux(p1++, IAny, 0, UCHAR_MAX);
    setinstaux(p1++, IAny, 0, n);
    return (p1 - p);
}

static int init_any(PyObject *self, Py_ssize_t n) {
    if (n == 0) {
        /* Match the null string */
        if (resize_patt(self, 0) == -1)
            return -1;
        /* Nothing more to do */
    }
    else if (n > 0) {
        if (fill_any(self, n, 0, 0) == -1)
            return -1;
    }
    else if (-n <= UCHAR_MAX) {
        if (resize_patt(self, 2) == -1)
            return -1;
        setinstaux(patprog(self), IAny, 2, -n);
        setinst(patprog(self) + 1, IFail, 0);
    }
    else {
        int offset = 2;  /* space for IAny & IChoice */
        Instruction *p;
        offset = fill_any(self, -n - UCHAR_MAX, 3, offset);
        if (offset == -1)
            return -1;
        p = patprog(self);
        setinstaux(p, IAny, offset + 1, UCHAR_MAX);
        setinstaux(p + 1, IChoice, offset, UCHAR_MAX);
        setinst(p + offset, IFailTwice, 0);
    }
    return 0;
}

static int Pattern_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"match", "set", "range", NULL};
    PyObject *match = NULL;
    char *set = NULL;
    Py_ssize_t setlen = 0;
    char *range = NULL;
    Py_ssize_t rangelen = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Os#s#", kwlist,
                &match, &set, &setlen, &range, &rangelen))
        return -1;

    /* No arguments - just initialise an empty pattern */
    if (match == NULL && set == NULL && range == NULL) {
        Instruction *p = PyMem_New(Instruction, 1);
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(Instruction));
        setinst(p, IEnd, 0);
        patprog(self) = p;
        patlen(self) = 1;
        return 0;
    }

    /* Only one argument should be passed */
    if ((match && set) || (match && range) || (set && range)) {
        PyErr_SetString(PyExc_TypeError, "Only one of match, set or range can be specified");
        return -1;
    }

    if (set) {
        return init_set(self, set, setlen);
    }
    else if (range) {
        return init_range(self, range, rangelen);
    }
    else if (match == Py_None) {
        return init_fail(self);
    }
    else if (PyIndex_Check(match)) {
        PyObject *idx = PyNumber_Index(match);
        Py_ssize_t n;
        if (idx == NULL)
            goto invalid;
        n = PyInt_AsSsize_t(idx);
        Py_DECREF(idx);
        if (n == -1 && PyErr_Occurred())
            goto invalid;
        return init_any(self, n);
    }
    else if (PyString_Check(match)) {
        char *str;
        Py_ssize_t len;
        if (PyString_AsStringAndSize(match, &str, &len) == -1)
            goto invalid;
        return init_match(self, str, len);
    }
    else {
invalid:
        PyErr_SetString(PyExc_TypeError, "Pattern argument must be None, or convertible to a string or an integer");
        return -1;
    }

    return 0;
}

/* **********************************************************************
 * Environment management - convert a PyObject to an integer
 * **********************************************************************
 */
#define ENV_ERROR (-1)

static Py_ssize_t val2env(PyObject *patt, PyObject *val) {
    PyObject *env = patenv(patt);

    if (val == NULL)
        return 0;

    if (env == NULL) {
        env = PyList_New(0);
        if (env == NULL)
            return ENV_ERROR;
        patenv(patt) = env;
    }

    if (PyList_Append(env, val) == -1)
        return ENV_ERROR;

    /* Note - this is 1 more than the index.
     * This is deliberate, so that 0 can be used to represent NULL (which is a
     * common case)
     */
    return PyList_Size(env);
}

static PyObject *env2val(PyObject *patt, Py_ssize_t idx) {
    PyObject *env = patenv(patt);
    PyObject *result;

    if (idx == 0)
        return NULL;

    /* The index is 1 higher than the list position (see above) */
    D1("Looking up index %d", idx);
    --idx;
    result = PySequence_GetItem(env, idx);
    if (result == NULL)
        return PyErr_Format(PyExc_IndexError, "Pattern env index out of range");

    D1("Result is %p", result);
    return result;
}

/* **********************************************************************
 * Pattern verifier
 * **********************************************************************
 */
static Py_ssize_t getposition(PyObject *patt, PyObject *positions, int i)
{
    PyObject *key = env2val(patt, i);
    PyObject *val;
    if (key == NULL)
        return -1;
    val = PyDict_GetItem(positions, key);
    if (val == NULL)
        return -1;
    return PyInt_AsSsize_t(val);
}

/* TODO: Fix this up */
void set_error_id(PyObject *exc, const char *format, PyObject *id) {
    PyObject *str = NULL;
    char *s = "<invalid>";
    if (id) {
        str = PyObject_Str(id);
        s = PyString_AS_STRING(str);
        Py_DECREF(str);
    }
    PyErr_Format(exc, format, s);
}

/* Return: -1=error, 0=possible infinite loop, 1=valid */
static int verify (PyObject *patt, Instruction *op, const Instruction *p,
                   Instruction *e, PyObject *positions, PyObject *id) {
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
                if (backtop >= MAXBACK) {
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
                if (backtop >= MAXBACK) {
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
                        set_error_id(PyExc_RuntimeError, "Rule %s is left recursive", id);
                        return -1;
                    }
                }
                if (backtop >= MAXBACK) {
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

static int checkrule (PyObject *patt, int from, int to, PyObject *positions, PyObject *id) {
    int i;
    int lastopen = 0;  /* more recent OpenCall seen in the code */
    Instruction *op = patprog(patt);
    for (i = from; i < to; i += sizei(op + i)) {
        if (op[i].i.code == IPartialCommit && op[i].i.offset < 0) {  /* loop? */
            int start = dest(op, i);
            assert(op[start - 1].i.code == IChoice && dest(op, start - 1) == i + 1);
            if (start <= lastopen) {  /* loop does contain an open call? */
                /* check body */
                switch (verify(patt, op, op + start, op + i, positions, id)) {
                    case 0:
                        set_error_id(PyExc_RuntimeError, "Possible infinite loop in rule %s", id);
                        /* Fall through */
                    case -1:
                        return -1;
                    default: /* result is valid, so do nothing */
                        break;
                }
            }
        }
        else if (op[i].i.code == IOpenCall)
            lastopen = i;
    }
    assert(op[i - 1].i.code == IRet);
    if (verify(patt, op, op + from, op + to - 1, positions, id) == -1)
        return -1;
    return 0;
}

/* **********************************************************************
 * Constructors
 * **********************************************************************
 */
static PyObject *Pattern_Set(PyObject *cls, PyObject *arg) {
    char *str;
    Py_ssize_t len;
    PyObject *result;

    if (PyString_AsStringAndSize(arg, &str, &len) == -1)
        return NULL;
    result = new_patt(cls, 0);
    if (result == NULL)
        return NULL;
    if (init_set(result, str, len) == -1) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject *Pattern_Range(PyObject *cls, PyObject *arg)
{
    char *str;
    Py_ssize_t len;
    PyObject *result;

    if (PyString_AsStringAndSize(arg, &str, &len) == -1)
        return NULL;
    result = new_patt(cls, 0);
    if (result == NULL)
        return NULL;
    if (init_range(result, str, len) == -1) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject *Pattern_Any(PyObject *cls, PyObject *arg)
{
    long n = PyInt_AsLong(arg);
    PyObject *result;

    if (n == -1 && PyErr_Occurred())
        return NULL;
    result = new_patt(cls, 0);
    if (result == NULL)
        return NULL;
    if (init_any(result, n) == -1) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject *Pattern_Match(PyObject *cls, PyObject *arg) {
    char *str;
    Py_ssize_t len;
    PyObject *result;

    if (PyString_AsStringAndSize(arg, &str, &len) == -1)
        return NULL;
    result = new_patt(cls, 0);
    if (result == NULL)
        return NULL;
    if (init_match(result, str, len) == -1) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject *Pattern_Fail(PyObject *cls) {
    PyObject *result = new_patt(cls, 0);
    if (result == NULL)
        return NULL;
    if (init_fail(result) == -1) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject *Pattern_Dummy(PyObject *cls) {
    PyObject *result = new_patt(cls, sizeof(Dummy)/sizeof(*Dummy));
    if (result)
        memcpy(patprog(result), Dummy, sizeof(Dummy));
    return result;
}

/* **********************************************************************
 * Grammar creation
 * **********************************************************************
 */
static PyObject *Pattern_Var (PyObject *cls, PyObject *name) {
    PyObject *result = new_patt(cls, 1);
    Py_ssize_t idx;
    if (result == NULL)
        return NULL;
    idx = val2env(result, name);
    if (idx == ENV_ERROR) {
        Py_DECREF(result);
        return NULL;
    }
    setinst(patprog(result), IOpenCall, idx);
    return result;
}

typedef int (*LoopFn) (PyObject *key, PyObject *val, void *extra);

/* Loop over all arguments to a Python function. Note that args is always a
 * tuple, and kw is always a dict. However, kw can be NULL, but args cannot.
 */
static int loop_args(PyObject *args, PyObject *kw, LoopFn handler, void *extra) {
    PyObject *key;
    PyObject *val;
    Py_ssize_t i;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);

    for (i = 0; i < nargs; ++i) {
        val = PyTuple_GET_ITEM(args, i);
        key = PyInt_FromSsize_t(i);
        if (key == NULL)
            return -1;
        if (handler(key, val, extra) == -1) {
            Py_DECREF(key);
            return -1;
        }
    }

    i = 0;
    while (kw && PyDict_Next(kw, &i, &key, &val)) {
        if (handler(key, val, extra) == -1)
            return -1;
    }

    return 0;
}

typedef struct RuleData {
    Py_ssize_t totalsize;
    PyObject *ruleids;
    PyObject *rules;
    PyObject *positions;
} RuleData;

static int init_rule_data(RuleData *r, Py_ssize_t totalsize) {
    r->totalsize = totalsize;
    r->rules = PyList_New(0);
    if (r->rules == NULL)
        return -1;
    r->ruleids = PyList_New(0);
    if (r->ruleids == NULL) {
        Py_DECREF(r->rules);
        return -1;
    }
    r->positions = PyDict_New();
    if (r->positions == NULL) {
        Py_DECREF(r->rules);
        Py_DECREF(r->ruleids);
        return -1;
    }
    return 0;
}

static void free_rule_data(RuleData *r) {
    Py_XDECREF(r->rules);
    Py_XDECREF(r->ruleids);
    Py_XDECREF(r->positions);
}

static int add_rule(PyObject *key, PyObject *val, void *extra) {
    RuleData *r = (RuleData *)extra;
    PyObject *py_ts;

    if (!PyObject_IsInstance(val, pattern_cls)) {
        PyErr_SetString(PyExc_TypeError, "Grammar rule must be a pattern");
        return -1;
    }

    py_ts = PyInt_FromSsize_t(r->totalsize);
    if (py_ts == NULL)
        return -1;
    if (PyDict_SetItem(r->positions, key, py_ts)) {
        Py_DECREF(py_ts);
        return -1;
    }
    Py_DECREF(py_ts);
    if (PyList_Append(r->ruleids, key) == -1)
        return -1;
    if (PyList_Append(r->rules, val) == -1)
        return -1;

    /* Add space for pattern + RET */
    r->totalsize += patsize(val) + 1;
    return 0;
}

static PyObject *Pattern_Grammar (PyObject *cls, PyObject *args, PyObject *kw) {
    PyObject *result = NULL;
    Py_ssize_t i;
    Instruction *p;
    PyObject *init_rule = NULL;
    PyObject *initpos;
    Py_ssize_t pos;
    RuleData r;

    /* (1) Initialise working storage.
     * Totalsize starts at 2, to cater for initial call and jump
     */
    if (init_rule_data(&r, 2) == -1)
        return NULL;

    /* (2) Parse the start rule out of any keyword arguments, or default to 0 */
    if (kw && (init_rule = PyDict_GetItemString(kw, "start")) != NULL) {
        Py_INCREF(init_rule);
        if (PyDict_DelItemString(kw, "start") == -1)
            goto err;
    }
    else {
        init_rule = PyInt_FromLong(0);
        if (init_rule == NULL)
            goto err;
    }

    /* (3) Loop through the arguments, adding each rule to the rule lists */
    if (loop_args(args, kw, add_rule, &r) == -1)
        goto err;

    /* Check that there was at least 1 rule */
    if (PyList_GET_SIZE(r.rules) == 0) {
        PyErr_SetString(PyExc_ValueError, "Empty grammar");
        goto err;
    }

    /* (4) Build the pattern */
    result = new_patt(cls, r.totalsize);
    if (result == NULL)
        goto err;
    p = patprog(result);
    ++p; /* Leave space for call */
    setinst(p++, IJmp, r.totalsize - 1);  /* after call, jumps to the end */

    for (i = 0; i < PyList_GET_SIZE(r.rules); ++i) {
        PyObject *patt = PyList_GET_ITEM(r.rules, i);
        p += addpatt(result, p, patt);
        setinst(p++, IRet, 0);
    }
    /* Go back to first position */
    p = patprog(result);

    /* (5) Check the patterns */
    for (pos = 2, i = 0; i < PyList_GET_SIZE(r.rules); i++) {  /* check all rules */
        PyObject *patt = PyList_GET_ITEM(r.rules, i);
        PyObject *ruleid = PyList_GET_ITEM(r.ruleids, i);
        Py_ssize_t len = patsize(patt) + 1;
        /* Rule is only needed for error message */
        if (checkrule(result, pos, pos + len, r.positions, ruleid) == -1)
            goto err;
        pos += len;
    }

    /* (6) Check that the initial rule is valid, and set up the call */
    initpos = PyDict_GetItem(r.positions, init_rule);
    if (initpos == NULL) {
        PyErr_SetString(PyExc_ValueError, "Initial rule is not defined in the grammar");
        goto err;
    }
    pos = PyInt_AS_LONG(initpos);
    setinst(p, ICall, pos);  /* first instruction calls initial rule */

    /* (7) Correct any open calls (note tail call optimisation here) */
    for (i = 0; i < r.totalsize; i += sizei(p + i)) {
        if (p[i].i.code == IOpenCall) {
            int pos = getposition(result, r.positions, p[i].i.offset);
            D2("Pos is %d, patching in %d", pos, pos-i);
            if (pos == -1 && PyErr_Occurred())
                goto err;
            p[i].i.code = (p[target(p, i + 1)].i.code == IRet) ? IJmp : ICall;
            p[i].i.offset = pos - i;
        }
    }
    optimizejumps(p);

    Py_DECREF(init_rule);
    free_rule_data(&r);
    return result;

err:
    Py_XDECREF(init_rule);
    free_rule_data(&r);
    Py_XDECREF(result);
    return NULL;
}

/* **********************************************************************
 * Pattern methods
 * **********************************************************************
 */
static PyObject *Pattern_env(PyObject* self) {
    PyObject *env = patenv(self);
    if (env == NULL)
        Py_RETURN_NONE;
    Py_INCREF(env);
    return env;
}

static PyObject *Pattern_display(Pattern* self) {
    printpatt(patprog(self));
    Py_RETURN_NONE;
}

static PyObject *Pattern_dump(Pattern *self) {
    PyObject *result = PyList_New(0);
    Instruction *p = patprog(self);

    if (result == NULL)
        return NULL;

    for (;;) {
        PyObject *item;
        char cset[256];
        int cs_len = 0;
        static char *kinds[] = {
            "Close", "Position", "Const", "Backref", "Arg", "Simple",
            "Table", "Function", "Query", "String", "Subst", "Fold",
            "Runtime", "Group" };

        if (hascharset(p)) {
            int i;
            for (i = 0; i < 256; ++i) {
                if (testchar((p+1)->buff, i)) {
                    cset[cs_len++] = i;
                }
            }
        }

        /* Instruction, aux, offset, cset, capkind, capoff, jmpdest */
        item = Py_BuildValue("(siis#sii)",
                INAME(p->i.code),
                p->i.aux, p->i.offset,
                cset, cs_len,
                iscapture(p) ? kinds[getkind(p)] : "",
                iscapture(p) ? getoff(p) : 0,
                isprop(p, ISJMP|ISCHECK) ? dest(0,p) == p ? -1 : dest(0,p) - patprog(self) : 0);
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

static PyObject *Pattern_set_code(Pattern* self, PyObject *args) {
    char *instr = NULL;
    Py_ssize_t instr_len = 0;
    PyObject *env = NULL;

    if (!PyArg_ParseTuple(args, "s#|O:_set_code", &instr, &instr_len, &env))
        return NULL;
    resize_patt((PyObject*)self, instr_len / sizeof(Instruction));
    memcpy(self->prog, instr, instr_len);
    self->env = env;
    Py_RETURN_NONE;
}

/* **********************************************************************
 * Pattern operators
 * **********************************************************************
 */
/* Rich comparison */
static PyObject *Pattern_richcompare(PyObject *self, PyObject *other, int op) {
    if (op != Py_EQ && op != Py_NE) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    /* Two patterns are equal if their code and length are equal */
    if (patlen(self) != patlen(other)) {
        goto ret_ne;
    }
    if (memcmp(patprog(self), patprog(other), patlen(self)) != 0) {
        goto ret_ne;
    }

    /* We're equal */
    if (op == Py_EQ)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;

ret_ne:
    if (op == Py_NE)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

/* Concatenate 2 patterns */
PyObject *Pattern_concat(PyObject *self, PyObject *other) {
    Instruction *p1;
    Instruction *p2;
    PyObject *result;

    if (ensure_patterns(&self, &other) == -1) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    p1 = patprog(self);
    p2 = patprog(other);

    if (isfail(p1) || issucc(p2)) {
        /* fail * x == fail; x * true == x */
        Py_DECREF(other);
        return self;
    }
    if (isfail(p2) || issucc(p1)) {
        /* true * x == x; x * fail == fail */
        Py_DECREF(self);
        return other;
    }
    if (isany(p1) && isany(p2)) {
        result = empty_patt(self, 0);
        if (result == NULL)
            goto ret;
        if (init_any(result, p1->i.aux + p2->i.aux) == -1) {
            Py_DECREF(result);
            result = NULL;
        }
    }
    else
    {
        Instruction *np;
        result = empty_patt(self, patsize(self) + patsize(other));
        np = patprog(result);
        if (result) {
            Instruction *p = np + addpatt(result, np, self);
            addpatt(result, p, other);
            optimizecaptures(np);
        }
    }
ret:
    Py_DECREF(self);
    Py_DECREF(other);
    return result;
}

/* Assert that pattern self matches at the current position */
PyObject *Pattern_and(PyObject *self) {
    Instruction *p1 = patprog(self);
    CharsetTag st1;
    PyObject *result;

    if (isfail(p1) || issucc(p1)) {
        /* &fail == fail; &true == true */
        Py_INCREF(self);
        return self;
    }

    if (tocharset(p1, &st1) == ISCHARSET) {
        result = empty_patt(self, CHARSETINSTSIZE + 1);
        if (result) {
            Instruction *p = patprog(result);
            setinst(p, ISet, CHARSETINSTSIZE + 1);
            loopset(i, p[1].buff[i] = ~st1.cs[i]);
            setinst(p + CHARSETINSTSIZE, IFail, 0);
        }
    }
    else {
        Py_ssize_t l1 = patsize(self);
        result = empty_patt(self, 1 + l1 + 2);
        if (result) {
            Instruction *p = patprog(result);
            setinst(p++, IChoice, 1 + l1 + 1);
            p += addpatt(result, p, self);
            setinst(p++, IBackCommit, 2);
            setinst(p, IFail, 0);
        }
    }

    return result;
}

/* Match self, as long as other does not match */
static PyObject *Pattern_diff(PyObject *self, PyObject *other) {
    PyObject *result;
    CharsetTag st1, st2;

    /* Make sure both arguments are patterns */
    if (ensure_patterns(&self, &other) == -1) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    if (tocharset(patprog(self), &st1) == ISCHARSET &&
            tocharset(patprog(other), &st2) == ISCHARSET) {
        result = empty_charset(self);
        if (result)
            loopset(i, patprog(result)[1].buff[i] = st1.cs[i] & ~st2.cs[i]);
    }
    else if (isheadfail(patprog(other))) {
        Py_ssize_t l1 = patsize(self);
        Py_ssize_t l2 = patsize(other);
        result = empty_patt(self, l2 + 1 + l1);
        if (result) {
            Instruction *p = patprog(result);
            p += addpatt(result, p, other);
            check2test(p - l2, l2 + 1);
            setinst(p++, IFail, 0);
            addpatt(result, p, self);
        }
    }
    else {  /* !e2 . e1 */
        /* !e -> choice L1; e; failtwice; L1: ... */
        Py_ssize_t l1 = patsize(self);
        Py_ssize_t l2 = patsize(other);
        result = empty_patt(self, 1 + l2 + 1 + l1);
        if (result) {
            Instruction *p = patprog(result);
            Instruction *pi = p;
            setinst(p++, IChoice, 1 + l2 + 1);
            p += addpatt(result, p, other);
            setinst(p++, IFailTwice, 0);
            addpatt(result, p, self);
            optimizechoice(pi);
        }
    }

    Py_DECREF(self);
    Py_DECREF(other);
    return result;
}

/* Assert that self does not match here */
static PyObject *Pattern_negate (PyObject *self) {
    Instruction *p = patprog(self);
    PyObject *result;

    if (isfail(p)) {  /* -false? */
        result = empty_patt(self, 0); /* true */
    }
    else if (issucc(p)) {  /* -true? */
        result = empty_patt(self, 1);  /* false */
        if (result)
            setinst(patprog(result), IFail, 0);
    }
    else {  /* -A == true - A */
        result = empty_patt(self, 0); /* true */
        if (result)
            result = Pattern_diff(result, self);
    }

    return result;
}

/* Helper functions for repetition operators. There are 5 helpers:
 *   - repeatcharset: >= n occurrences of a characterset
 *   - repeatheadfail: >= n of a test instruction (head fail optimisation)
 *   - repeats: >= n of any other pattern
 *   - optionalheadfail: <= n of a test instruction (head fail optimisation)
 *   - optionals: <= n of any other pattern
 */
static PyObject *repeatcharset (PyObject *patt, Charset cs, Py_ssize_t n) {
    /* e; ...; e; span; */
    int i;
    Instruction *p;
    PyObject *result = empty_patt(patt, n * patsize(patt) + CHARSETINSTSIZE);
    if (result == NULL)
        return NULL;
    p = patprog(result);
    for (i = 0; i < n; i++) {
        p += addpatt(result, p, patt);
    }
    setinst(p, ISpan, 0);
    loopset(k, p[1].buff[k] = cs[k]);
    return result;
}

static PyObject *repeatheadfail (PyObject *patt, int n) {
    /* e; ...; e; L2: e'(L1); jump L2; L1: ... */
    int i;
    Instruction *p;
    PyObject *result;
    Py_ssize_t len = patsize(patt);

    result = empty_patt(patt, (n + 1) * len + 1);
    if (result == NULL)
        return NULL;
    p = patprog(result);
    for (i = 0; i < n; i++) {
        p += addpatt(result, p, patt);
    }
    p += addpatt(result, p, patt);
    check2test(p - len, len + 1);
    setinst(p, IJmp, -len);
    return result;
}

static PyObject *repeats(PyObject *patt, Py_ssize_t n) {
  /* e; ...; e; choice L1; L2: e; partialcommit L2; L1: ... */
    int i;
    Instruction *p;
    PyObject *result;
    Py_ssize_t len = patsize(patt);
    
    result = empty_patt(patt, (n + 1) * len + 2);
    if (result == NULL)
        return NULL;

    /* Note - in verifier, there is a commented-out piece of code relating to
     * fenv lookups which needs fixing. This appears to be only relevant for
     * grammars.
     * TODO: Fix this when implementing grammars.
     */
    p = patprog(patt);
    switch (verify(result, p, p, p + len, NULL, NULL)) {
        case 0:
            PyErr_SetString(PyExc_ValueError, "Loop body may accept empty string");
            /* Fall through */
        case -1:
            Py_DECREF(result);
            return NULL;
        default: /* result is valid, so do nothing */
            break;
    }

    p = patprog(result);
    for (i = 0; i < n; i++) {
        p += addpatt(result, p, patt);
    }
    setinst(p++, IChoice, 1 + len + 1);
    p += addpatt(result, p, patt);
    setinst(p, IPartialCommit, -len);
    return result;
}

static PyObject *optionalheadfail(PyObject *patt, int n) {
    Instruction *p;
    Py_ssize_t len = patsize(patt);
    PyObject *result = empty_patt(patt, n * len);
    int i;
    if (result == NULL)
        return NULL;
    p = patprog(result);
    for (i = 0; i < n; i++) {
        p += addpatt(result, p, patt);
        check2test(p - len, (n - i) * len);
    }
    return result;
}

static PyObject *optionals(PyObject *patt, int n) {
    /* choice L1; e; partialcommit L2; L2: ... e; L1: commit L3; L3: ... */
    int i;
    Py_ssize_t len = patsize(patt);
    Instruction *p;
    PyObject *result = empty_patt(patt, n * (len + 1) + 1);
    if (result == NULL)
        return NULL;
    p = patprog(result);
    setinst(p++, IChoice, 1 + n * (len + 1));
    for (i = 0; i < n; i++) {
        p += addpatt(result, p, patt);
        setinst(p++, IPartialCommit, 1);
    }
    setinst(p - 1, ICommit, 1);  /* correct last commit */
    optimizechoice(patprog(result));
    return result;
}

/* Repetition operator */
static PyObject *Pattern_pow (PyObject *self, PyObject *other, PyObject *modulo) {
    long n = PyInt_AsLong(other);
    Instruction *p1 = patprog(self);
    /* Ignore modulo argument - not meaningful */

    if (n == -1 && PyErr_Occurred())
        return NULL;

    if (n >= 0) {
        CharsetTag st;
        Instruction *op;
        PyObject *result;
        if (tocharset(p1, &st) == ISCHARSET)
            return repeatcharset(self, st.cs, n);
        if (isheadfail(p1))
            result = repeatheadfail(self, n);
        else
            result = repeats(self, n);
        if (!result)
            return result;
        op = patprog(result);
        optimizecaptures(op);
        optimizejumps(op);
        return result;
    }
    else {
        if (isheadfail(p1))
            return optionalheadfail(self, -n);
        else
            return optionals(self, -n);
    }
}

/* Helper functions for ordered choice operator */

/* Create a new empty pattern, size *size + extra. Copy self's environment
 * across. Add extra to *size and set *pptr to start of extra space.
 */
static PyObject *auxnew (PyObject *self, int *size, int extra, Instruction **pptr) {
    PyObject *result;

    result = empty_patt(self, *size + extra);
    if (result == NULL)
        return NULL;
    mergeenv(result, self);
    *size += extra;
    *pptr = patprog(result) + *size - extra;
    return result;
}

static PyObject *basic_union (PyObject *self, PyObject *other,
        Instruction *p1, int l1, int *size, CharsetTag *st2) {
    PyObject *result;
    CharsetTag st1;
    tocharset(p1, &st1);
    if (st1.tag == ISCHARSET && st2->tag == ISCHARSET) {
        Instruction *p;
        result = auxnew(self, size, CHARSETINSTSIZE, &p);
        setinst(p, ISet, 0);
        loopset(i, p[1].buff[i] = st1.cs[i] | st2->cs[i]);
    }
    else if (exclusive(&st1, st2) || isheadfail(p1)) {
        Instruction *p;
        result = auxnew(self, size, l1 + 1 + patsize(other), &p);
        if (result == NULL)
            return result;
        copypatt(p, p1, l1);
        check2test(p, l1 + 1);
        p += l1;
        setinst(p++, IJmp, patsize(other) + 1);
        addpatt(result, p, other);
    }
    else {
        /* choice L1; e1; commit L2; L1: e2; L2: ... */
        Instruction *p;
        result = auxnew(self, size, 1 + l1 + 1 + patsize(other), &p);
        if (result == NULL)
            return NULL;
        setinst(p++, IChoice, 1 + l1 + 1);
        copypatt(p, p1, l1); p += l1;
        setinst(p++, ICommit, 1 + patsize(other));
        addpatt(result, p, other);
        optimizechoice(p - (1 + l1 + 1));
    }
    return result;
}

/* p1/l1 are the start and length of self. size is 0. st2 is the charset tag
 * for other.
 * Firstpart: {TEST->L xxxxx JMP/COMMIT->E L: xxxxx E:} = L
 *            {CHOICE->L xxxxx COMMIT->E: L: xxxxx E:} = L
 *            else 0
 * If self is all 1 part, union with other.
 * Else, if part 1 ends in commit and doesn't interfere with part 2,
 *   return part1 + part2 (with part1 commit adjusted)
 * Else,
 *   return (test1 choice restofp1 commit) + part2
 */
static PyObject *separateparts (PyObject *self, PyObject *other, 
        Instruction *p1, int l1, int *size, CharsetTag *st2) {
    int sp = firstpart(p1, l1);
    if (sp == 0) /* first part is entire p1? */
        return basic_union(self, other, p1, l1, size, st2);
    else if ((p1 + sp - 1)->i.code == ICommit || !interfere(p1, sp, st2)) {
        Instruction *p;
        int init = *size;
        int end = init + sp;
        *size = end;
        PyObject *result = separateparts(self, other, p1 + sp, l1 - sp, size, st2);
        if (result == NULL)
            return NULL;
        p = patprog(result);
        copypatt(p + init, p1, sp);
        (p + end - 1)->i.offset = *size - (end - 1);
        return result;
    }
    else {  /* must change back to non-optimized choice */
        Instruction *p;
        int init = *size;
        int end = init + sp + 1; /* Needs 1 extra instruction (choice) */
        int sizefirst = sizei(p1); /* Size of p1's first instruction (the test) */
        *size = end;
        PyObject *result = separateparts(self, other, p1 + sp, l1 - sp, size, st2);
        if (result == NULL)
            return NULL;
        p = patprog(result);
        copypatt(p + init, p1, sizefirst); /* Copy the test */
        (p + init)->i.offset++; /* Correct jump (because of new instruction) */
        init += sizefirst;
        setinstaux(p + init, IChoice, sp - sizefirst + 1, 1);
        init++;
        copypatt(p + init, p1 + sizefirst, sp - sizefirst - 1);
        init += sp - sizefirst - 1;
        setinst(p + init, ICommit, *size - (end - 1));
        return result;
    }
}

/* Ordered choice operator */
static PyObject *Pattern_or (PyObject *self, PyObject *other) {
    int size = 0;
    Instruction *p1;
    Instruction *p2;
    CharsetTag st2;
    PyObject *result;
    /* Make sure both arguments are patterns */
    if (ensure_patterns(&self, &other) == -1) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    p1 = patprog(self);
    p2 = patprog(other);
    if (isfail(p1)) { /* check for simple identities */
        Py_DECREF(self);
        return other; /* fail / a == a */
    }
    else if (isfail(p2) || issucc(p1)) {
        Py_DECREF(other);
        return self; /* a / fail == a; true / a == true */
    }
    else {
        tocharset(p2, &st2);
        result = separateparts(self, other, p1, patsize(self), &size, &st2);
    }
    Py_DECREF(self);
    Py_DECREF(other);
    return result;
}

int ensure_pattern(PyObject **pat) {
    if (!PyObject_IsInstance(*pat, pattern_cls)) {
        PyObject *result = PyObject_CallFunctionObjArgs(pattern_cls, *pat, NULL);
        if (result == NULL)
            return -1;
        *pat = result;
        Py_INCREF(*pat);
    }
    return 0;
}

/* **********************************************************************
 * Captures - creation of capture instructions
 * **********************************************************************
 */
static PyObject *capture_aux(PyObject*cls, PyObject *pat, int kind, PyObject *label) {
    int n;
    int lc;
    PyObject *result;

    if (ensure_pattern(&pat) == -1) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    lc = skipchecks(patprog(pat), 0, &n);

    if (lc == patsize(pat)) {  /* got whole pattern? */
        /* may use a IFullCapture instruction at its end */
        result = new_patt(cls, patsize(pat) + 1);
        if (result) {
            Instruction *p = patprog(result);
            int labelid = val2env(result, label);
            if (labelid == ENV_ERROR) {
                Py_DECREF(result);
                return NULL;
            }
            p += addpatt(result, p, pat);
            setinstcap(p, IFullCapture, labelid, kind, n);
        }
    }
    else {  /* must use open-close pair */
        result = new_patt(cls, 1 + patsize(pat) + 1);
        if (result) {
            Instruction *p = patprog(result);
            int labelid = val2env(result, label);
            if (labelid == ENV_ERROR) {
                Py_DECREF(result);
                return NULL;
            }
            setinstcap(p++, IOpenCapture, labelid, kind, 0);
            p += addpatt(result, p, pat);
            setinstcap(p, ICloseCapture, 0, Cclose, 0);
            optimizecaptures(patprog(result));
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

static PyObject *Pattern_CaptureGroup(PyObject *cls, PyObject *args) {
    PyObject *pat = NULL;
    PyObject *id = NULL;

    if (!PyArg_UnpackTuple(args, "CapG", 1, 2, &pat, &id))
        return NULL;
    return capture_aux(cls, pat, Cgroup, id);
}

static PyObject *Pattern_CaptureFold(PyObject *cls, PyObject *args) {
    PyObject *pat = NULL;
    PyObject *fn = NULL;

    if (!PyArg_UnpackTuple(args, "CapF", 2, 2, &pat, &fn))
        return NULL;
    return capture_aux(cls, pat, Cfold, fn);
}

static PyObject *Pattern_CaptureRuntime(PyObject *cls, PyObject *args) {
    PyObject *pat = NULL;
    PyObject *fn = NULL;
    PyObject *result;

    if (!PyArg_UnpackTuple(args, "CapRT", 2, 2, &pat, &fn))
        return NULL;
    if (ensure_pattern(&pat) == -1) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    result = new_patt(cls, 1 + patsize(pat) + 1);
    if (result) {
        Instruction *p = patprog(result);
        int id = val2env(result, fn);
        if (id == ENV_ERROR) {
            Py_DECREF(result);
            return NULL;
        }
        setinstcap(p++, IOpenCapture, id, Cruntime, 0);
        p += addpatt(result, p, pat);
        setinstcap(p, ICloseRunTime, 0, Cclose, 0);
        optimizecaptures(patprog(result));
    }

    return result;
}

static PyObject *Pattern_divide(PyObject *self, PyObject *other) {
    PyObject *cls;
    PyObject *result = NULL;

    cls = PyObject_Type(self);
    if (cls == NULL)
        return NULL;

    if (PyString_Check(other))
        result = capture_aux(((PyObject*)(self->ob_type)), self, Cstring, other);
    else if (PyMapping_Check(other))
        result = capture_aux(((PyObject*)(self->ob_type)), self, Cquery, other);
    else if (1) /* No way of testing for callable! */
        result = capture_aux(((PyObject*)(self->ob_type)), self, Cfunction, other);
    else
        PyErr_SetString(PyExc_ValueError, "Pattern replacement must be string, function, or dict");

    Py_DECREF(cls);
    return result;
}

static PyObject *Pattern_CapturePos(PyObject *cls) {
    PyObject *result = new_patt(cls, 1);
    if (result)
        setinstcap(patprog(result), IEmptyCapture, 0, Cposition, 0);
    return result;
}

static PyObject *Pattern_CaptureBack(PyObject *cls, PyObject *id) {
    PyObject *result = new_patt(cls, 1);
    if (result) {
        Py_ssize_t j = val2env(result, id);
        if (j == ENV_ERROR) {
            Py_DECREF(result);
            return NULL;
        }
        setinstcap(patprog(result), IEmptyCaptureIdx, j, Cbackref, 0);
    }
    return result;
}

static PyObject *Pattern_CaptureArg(PyObject *cls, PyObject *id) {
    long n = PyInt_AsLong(id);
    PyObject *result;
    if (n == -1 && PyErr_Occurred())
        return NULL;
    if (n <= 0 || n > SHRT_MAX) {
        PyErr_SetString(PyExc_ValueError, "Argument ID out of range");
        return NULL;
    }
    result = new_patt(cls, 1);
    if (result)
        setinstcap(patprog(result), IEmptyCapture, n, Carg, 0);
    return result;
}

static PyObject *Pattern_CaptureConst(PyObject *cls, PyObject *args) {
    Py_ssize_t i, j;
    Py_ssize_t n = PyTuple_GET_SIZE(args);
    Instruction *p;
    PyObject *val;
    PyObject *result = new_patt(cls, n > 1 ? n + 2 : n);
    if (result == NULL) {
        return NULL;
    }
    p = patprog(result);
    if (n > 1) setinstcap(p++, IOpenCapture, 0, Cgroup, 0);
    for (i = 0; i < n; i++) {
        val = PyTuple_GET_ITEM(args, i);
        j = val2env(result, val);
        if (j == ENV_ERROR) {
            Py_DECREF(result);
            return NULL;
        }
        setinstcap(p++, IEmptyCaptureIdx, j, Cconst, 0);
    }
    if (n > 1) setinstcap(p++, ICloseCapture, 0, Cclose, 0);
    return result;
}

/* **********************************************************************
 * Captures - post-match capturing of values
 * **********************************************************************
 */
int pushsubject(CapState *cs, Capture *c) {
    PyObject *str = PyString_FromStringAndSize(c->s, c->siz - 1);
    int ret = 0;
    if (str == NULL)
        return -1;
    if (PyList_Append(cs->values, str) == -1)
        ret = -1;
    Py_DECREF(str);
    return ret;
}

static int pushcapture (CapState *cs);

static PyObject *getonecapture (CapState *cs) {
    int n;
    PyObject *save;
    PyObject *captures = PyList_New(0);
    if (captures == NULL)
        return NULL;
    save = cs->values;
    cs->values = captures;
    n = pushcapture(cs);
    cs->values = save;
    if (n == -1) {
        Py_DECREF(captures);
        return NULL;
    }
    save = PySequence_GetItem(captures, 0);
    Py_DECREF(captures);
    return save;
}

static int pushallvalues (CapState *cs, int addextra) {
    Capture *co = cs->cap;
    int n = 0;
    if (isfullcap(cs->cap++)) {
        if (pushsubject(cs, co) == -1) /* Push whole match */
            return -1;
        return 1;
    }
    while (!isclosecap(cs->cap))
        n += pushcapture(cs);
    if (addextra || n == 0) {  /* need extra? */
        PyObject *str = PyString_FromStringAndSize(co->s, cs->cap->s - co->s);
        if (str == NULL)
            return -1;
        if (PyList_Append(cs->values, str) == -1) {
            Py_DECREF(str);
            return -1;
        }
        Py_DECREF(str);
        /* TODO Why is this a pre-increment? lpeg.c uses a post-increment */
        ++n;
    }
    cs->cap++;
    return n;
}

static int addonestring (PyObject *lst, CapState *cs, const char *what);

static int stringcap(PyObject *lst, CapState *cs) {
    StrAux cps[MAXSTRCAPS];
    int n;
    Py_ssize_t len, i;
    char *c;
    PyObject *str;
    /* TODO: Cache management needs consideration!
    updatecache(cs, cs->cap->idx);
    c = lua_tolstring(cs->L, subscache(cs), &len);
    */
    str = env2val(cs->patt, cs->cap->idx);
    PyString_AsStringAndSize(str, &c, &len);
    n = getstrcaps(cs, cps, 0) - 1;
    for (i = 0; i < len; i++) {
        if (c[i] != '%' || c[++i] < '0' || c[i] > '9') {
            /* Add 1 char, c[i] */
            str = PyString_FromStringAndSize(&c[i], 1);
            PyList_Append(lst, str);
            Py_DECREF(str);
        }
        else {
            int l = c[i] - '0';
            if (l > n) {
                PyErr_SetString(PyExc_ValueError, "Invalid capture index");
                return -1;
            }
            else if (cps[l].isstring) {
                str = PyString_FromStringAndSize(cps[l].u.s.s, cps[l].u.s.e - cps[l].u.s.s);
                PyList_Append(lst, str);
                Py_DECREF(str);
            }
            else {
                Capture *curr = cs->cap;
                int rv;
                cs->cap = cps[l].u.cp;
                rv = addonestring(lst, cs, "capture");
                if (rv != 1) {
                    if (rv != -1)
                        PyErr_SetString(PyExc_ValueError, "No values in capture index");
                    return -1;
                }
                cs->cap = curr;
            }
        }
    }
    return 0;
}

static int substcap(PyObject *lst, CapState *cs) {
    const char *curr = cs->cap->s;
    PyObject *str;
    if (isfullcap(cs->cap)) {
        /* Keep original text */
        str = PyString_FromStringAndSize(curr, cs->cap->siz - 1);
        if (str == NULL) {
            return -1;
        }
        if (PyList_Append(lst, str)) {
            Py_DECREF(str);
            return -1;
        }
        Py_DECREF(str);
    }
    else {
        cs->cap++;
        while (!isclosecap(cs->cap)) {
            const char *next = cs->cap->s;
            int rv;
            str = PyString_FromStringAndSize(curr, next - curr);
            if (str == NULL) {
                return -1;
            }
            if (PyList_Append(lst, str)) {
                Py_DECREF(str);
                return -1;
            }
            Py_DECREF(str);
            rv = addonestring(lst, cs, "replacement");
            if (rv == -1)
                return -1;
            else if (rv == 0) /* No capture value? */
                curr = next;
            else
                curr = closeaddr(cs->cap - 1); /* Continue after match */
        }
        str = PyString_FromStringAndSize(curr, cs->cap->s - curr); /* Add last piece of text */
        if (str == NULL) {
            return -1;
        }
        if (PyList_Append(lst, str)) {
            Py_DECREF(str);
            return -1;
        }
        Py_DECREF(str);
    }
    cs->cap++; /* Go to next capture */
    return 0;
}

static Capture *findback (CapState *cs, Capture *cap, PyObject *id) {
    for (;;) {
        if (cap == cs->ocap) {  /* not found */
            PyObject *repr = PyObject_Repr(id);
            char *s;
            if (repr == NULL)
                return NULL;
            s = PyString_AsString(repr);
            if (s == NULL)
                s = "(Unknown)";
            PyErr_Format(PyExc_RuntimeError, "Back reference %s not found", s);
            Py_DECREF(repr);
            return NULL;
        }
        cap--;
        if (isclosecap(cap))
            cap = findopen(cap);
        else if (!isfullcap(cap))
            continue; /* opening an enclosing capture: skip and get previous */
        if (captype(cap) == Cgroup) {
            PyObject *grpid = env2val(cs->patt, cap->idx);
            int cmp;
            if (grpid == NULL && PyErr_Occurred())
                return NULL;
            if (PyObject_Cmp(id, grpid, &cmp) == -1) {
                Py_DECREF(grpid);
                return NULL;
            }
            if (cmp == 0) {  /* right group? */
                return cap;
            }
        }
    }
}

static int backrefcap (CapState *cs) {
    int n;
    Capture *curr = cs->cap;
    PyObject *id = env2val(cs->patt, cs->cap->idx);
    if (id == NULL && PyErr_Occurred())
        return -1;
    cs->cap = findback(cs, curr, id);
    if (cs->cap == NULL) {
        /* Restore old value and return an error */
        cs->cap = curr;
        return -1;
    }
    n = pushallvalues(cs, 0);
    cs->cap = curr + 1;
    return n;
}

static int tablecap (CapState *cs) {
    int n = 0;
    PyObject *result = PyList_New(0);
    if (result == NULL) {
        return -1;
    }
    if (PyList_Append(cs->values, result) == -1) {
        Py_DECREF(result);
        return -1;
    }
    if (isfullcap(cs->cap++)) {
        Py_DECREF(result);
        return 1; /* table is empty */
    }
    while (!isclosecap(cs->cap)) {
#if 0
        if (captype(cs->cap) == Cgroup && cs->cap->idx != 0 ) { /* named group? */
            int k;
            PyObject *id = env2val(cs->patt, cs->cap->idx);
            if (id == NULL && PyErr_Occurred())
                return -1;
            k = pushallvalues(cs, 0);
            if (k == 0) {
                continue;
            }
            else if (k > 1) {
        }
        else {
#endif
            int k = pushcapture(cs);
            Py_ssize_t values_len = PySequence_Size(cs->values);
            PyObject *slice = PySequence_GetSlice(cs->values, -k, values_len);
            if (slice == NULL) {
                Py_DECREF(result);
                return -1;
            }
            if (PySequence_SetSlice(result, n+1, n+1, slice) == -1) {
                Py_DECREF(slice);
                Py_DECREF(result);
                return -1;
            }
            if (PySequence_DelSlice(cs->values, -k, values_len) == -1) {
                Py_DECREF(slice);
                Py_DECREF(result);
                return -1;
            }
            Py_DECREF(slice);
            n += k;
#if 0
        }
#endif
    }
    cs->cap++;
    Py_DECREF(result);
    return 1;
}

static int querycap (CapState *cs) {
    int n;
    /* Copy this here, as pushallvalues changes cs->cap */
    int capidx = cs->cap->idx;
    PyObject *idx;
    PyObject *tbl;
    PyObject *result;
    n = pushallvalues(cs, 0);
    if (n == -1)
        return -1;
    /* We only want the first one (at posn -n) and we remove them
     * from the cs->values list
     */
    idx = PySequence_GetItem(cs->values, -n);
    if (idx == NULL)
        return -1;
    if (PySequence_DelSlice(cs->values, -n, PySequence_Size(cs->values)) == -1) {
        Py_DECREF(idx);
        return -1;
    }
    tbl = env2val(cs->patt, capidx);
    if (tbl == NULL) {
        Py_DECREF(idx);
        return -1;
    }
    if (!PyMapping_HasKey(tbl, idx)) {
        Py_DECREF(tbl);
        Py_DECREF(idx);
        return 0;
    }
    result = PyObject_GetItem(tbl, idx);
    if (result == NULL) {
        /* Cannot happen, as HasKey above returned true */
        Py_DECREF(tbl);
        Py_DECREF(idx);
        return -1;
    }
    Py_DECREF(tbl);
    Py_DECREF(idx);
    if (PyList_Append(cs->values, result) == -1) {
        Py_DECREF(result);
        return -1;
    }
    Py_DECREF(result);
    return 1;
}

static int functioncap (CapState *cs) {
    int n;
    int capidx = cs->cap->idx;
    PyObject *temp = cs->values;
    PyObject *captures;
    PyObject *fn;
    fn = env2val(cs->patt, capidx);
    if (fn == NULL && PyErr_Occurred())
        return -1;
    if (fn == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "No function for function capture");
        return -1;
    }
    captures = PyList_New(0);
    if (captures == NULL) {
        Py_DECREF(fn);
        return -1;
    }
    cs->values = captures;
    n = pushallvalues(cs, 0);
    cs->values = temp;
    temp = PyObject_CallFunctionObjArgs((PyObject*)&PyTuple_Type, captures, NULL);
    if (temp == NULL) {
        Py_DECREF(captures);
        Py_DECREF(fn);
        return -1;
    }
    Py_DECREF(captures);
    captures = temp;
    temp = PyObject_CallObject(fn, captures);
    Py_DECREF(captures);
    Py_DECREF(fn);
    if (temp == NULL)
        return -1;
    if (PyList_Append(cs->values, temp) == -1) {
        Py_DECREF(temp);
        return -1;
    }
    Py_DECREF(temp);
    return 1;
}

static int foldcap (CapState *cs) {
    int idx = cs->cap->idx;
    PyObject *accum;
    PyObject *fn = env2val(cs->patt, idx);
    if (fn == NULL && PyErr_Occurred())
        return -1;
    if (fn == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "No function for fold capture");
        return -1;
    }
    if (isfullcap(cs->cap++) || isclosecap(cs->cap) || (accum = getonecapture(cs)) == NULL) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_RuntimeError, "No initial value for fold capture");
        Py_DECREF(fn);
        return -1;
    }
    while (!isclosecap(cs->cap)) {
        PyObject *val = getonecapture(cs);
        accum = PyObject_CallFunctionObjArgs(fn, accum, val, NULL);
        Py_DECREF(val);
        if (accum == NULL) {
            Py_DECREF(fn);
            return -1;
        }
    }
    cs->cap++;  /* skip close entry */
    Py_DECREF(fn);
    if (PyList_Append(cs->values, accum) == -1) {
        Py_DECREF(accum);
        return -1;
    }
    Py_DECREF(accum);
    return 1;
}

static int runtimecap (Capture *close, Capture *ocap,
                       const char *o, const char *s,
                       PyObject *patt, PyObject *args,
                       PyObject **ret) {
    CapState cs;
    int n;
    PyObject *fn;
    Capture *open = findopen(close);
    PyObject *result = PyList_New(0);
    PyObject *temp;
    *ret = NULL;
    if (result == NULL)
        return -1;
    if (captype(open) != Cruntime) {
      PyErr_SetString(PyExc_RuntimeError, "Capture type is not runtime capture");
      return -1;
    }
    fn = env2val(patt, open->idx);
    if (fn == NULL) {
        Py_DECREF(result);
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_RuntimeError, "No function for runtime capture");
        return -1;
    }
    close->kind = Cclose;
    close->s = s;
    cs.ocap = ocap; cs.cap = open;
    cs.values = result;
    cs.s = o;
    cs.args = args;
    cs.patt = patt;
#if 0 /* What is this for? */
    pushluaval(&cs);
#endif
    n = pushallvalues(&cs, 0);
    temp = PyObject_CallFunctionObjArgs((PyObject*)&PyTuple_Type, result, NULL);
    Py_DECREF(result);
    if (temp == NULL) {
        Py_DECREF(fn);
        return -1;
    }
    result = temp;
    temp = PyObject_CallFunction(fn, "siO", o, s-o, result);
    Py_DECREF(result);
    Py_DECREF(fn);
    if (temp == NULL)
        return -1;
    *ret = temp;
    return close - open;
}
static int addonestring (PyObject *lst, CapState *cs, const char *what) {
    switch (captype(cs->cap)) {
        case Cstring:
            /* Add capture directly to buffer */
            if (stringcap(lst, cs) == -1)
                return -1;
            return 1;
        case Csubst:
            /* Add capture directly to buffer */
            if (substcap(lst, cs) == -1)
                return -1;
            return 1;
        default: {
            /* TODO: using cs->values as temp storage! */
#if 1 /* Less list hacking, but more temp value shuffling... */
            int n;
            PyObject *save = cs->values;
            PyObject *temp = PyList_New(0);
            if (temp == NULL)
                return -1;
            /* Only the first result */
            cs->values = temp;
            n = pushcapture(cs);
            if (n == -1) {
                Py_DECREF(temp);
                return -1;
            }
            temp = cs->values;
            cs->values = save;
            if (n > 0) {
                PyObject *val = PySequence_GetItem(temp, 0);
                Py_DECREF(temp);
                if (val == NULL)
                    return -1;
                if (!PyString_Check(val)) {
                    /* Convert to string */
                    PyObject *s = PyObject_Str(val);
                    Py_DECREF(val);
                    if (s == NULL)
                        return -1;
                    val = s;
                }
                if (PyList_Append(lst, val) == -1) {
                    Py_DECREF(val);
                    return -1;
                }
                Py_DECREF(val);
            }
            return n;
#else
            int n = pushcapture(cs);
            Py_ssize_t len = PyList_Size(cs->values);
            PyObject *val;
            /* Only the first result */
            val = PyList_GetItem(cs->values, len - n);
            if (!PyString_Check(val)) {
                /* Convert to string */
                PyObject *s = PyObject_Str(val);
                Py_DECREF(val);
                val = s;
            }
            PyList_Append(lst, val);
            /* Drop the results */
            PyList_SetSlice(cs->values, len - n, len, NULL);
            return n; /* ??? 1, surely? */
#endif
        }
    }
}

static int pushcapture (CapState *cs) {
    switch (captype(cs->cap)) {
        case Cposition: {
            long pos = cs->cap->s - cs->s;
            PyObject *val = PyInt_FromLong(pos);
            if (val == NULL)
                return -1;
            if (PyList_Append(cs->values, val) == -1) {
                Py_DECREF(val);
                return -1;
            }
            cs->cap++;
            return 1;
        }
        case Cconst: {
            int arg = (cs->cap++)->idx;
            PyObject *val = env2val(cs->patt, arg);
            /* What if arg == 0? */
            if (val == NULL)
                return -1;
            if (PyList_Append(cs->values, val) == -1) {
                Py_DECREF(val);
                return -1;
            }
            Py_DECREF(val);
            return 1;
        }
        case Carg: {
            int arg = (cs->cap++)->idx;
            PyObject *val = PySequence_GetItem(cs->args, arg);
            if (val == NULL)
                return -1;
            if (PyList_Append(cs->values, val) == -1) {
                Py_DECREF(val);
                return -1;
            }
            return 1;
        }
        case Csimple: {
            int k = pushallvalues(cs, 1);
            if (k == -1)
                return -1;
            if (k > 1) {
                /* Whole match is first result, so move the last element back
                 * to the start of the group we've just pushed
                 */
                PyObject *top = PySequence_GetItem(cs->values, -1);
                PyList_Insert(cs->values, -k, top);
                PySequence_DelItem(cs->values, -1);
                Py_DECREF(top);
            }
            return k;
        }
        case Cruntime: {
            int n = 0;
            while (!isclosecap(cs->cap++)) {
                PyObject *val = env2val(cs->patt, (cs->cap - 1)->idx);
                if (val == NULL) return -1;
                if (PyList_Append(cs->values, val) == -1) {
                    Py_DECREF(val);
                    return -1;
                }
                //luaL_checkstack(cs->L, 4, "too many captures");
                n++;
            }
            return n;
        }
        case Cstring: {
            PyObject *lst = PyList_New(0);
            PyObject *str = PyString_FromString("");
            PyObject *result;
            stringcap(lst, cs);
            result = PyObject_CallMethod(str, "join", "(O)", lst);
            PyList_Append(cs->values, result);
            Py_DECREF(result);
            Py_DECREF(str);
            Py_DECREF(lst);
            return 1;
        }
        case Csubst: {
            PyObject *lst = PyList_New(0);
            if (lst == NULL) {
                return -1;
            }
            PyObject *str = PyString_FromString("");
            if (str == NULL) {
                Py_DECREF(lst);
                return -1;
            }
            PyObject *result;
            if (substcap(lst, cs) == -1) {
                Py_DECREF(str);
                Py_DECREF(lst);
                return -1;
            }
            result = PyObject_CallMethod(str, "join", "(O)", lst);
            if (result == NULL) {
                Py_DECREF(str);
                Py_DECREF(lst);
                return -1;
            }
            if (PyList_Append(cs->values, result)) {
                Py_DECREF(result);
                Py_DECREF(str);
                Py_DECREF(lst);
                return -1;
            }
            Py_DECREF(result);
            Py_DECREF(str);
            Py_DECREF(lst);
            return 1;
        }
        case Cgroup: {
            if (cs->cap->idx == 0) /* Anonymous group? */
                return pushallvalues(cs, 0);
            else { /* Named group: add no values */
                cs->cap = nextcap(cs->cap);
                return 0;
            }
        }
        case Cbackref: return backrefcap(cs);
        /* Table captures have different semantics, because tables in
         * Lua don't quite correspond to lists or dicts in Python.
         */
        case Ctable: return tablecap(cs);
        case Cfunction: return functioncap(cs);
        case Cquery: return querycap(cs);
        case Cfold: return foldcap(cs);
        default: assert(0); return 0;
    }
}

static Capture *doublecap (Capture *cap, int captop) {
    Capture *newc;
    if (captop >= INT_MAX/((int)sizeof(Capture) * 2)) {
        PyErr_SetString(PyExc_OverflowError, "too many captures");
        return cap;
    }
    newc = realloc(cap, captop * 2 * sizeof(Capture));
    if (newc == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Couldn't expand the captures");
        return cap;
    }
    return newc;
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
        cs.args = args;
        cs.patt = patt;
        do { /* collect the values */
            int count = pushcapture(&cs);
            if (count == -1) {
                Py_DECREF(result);
                return NULL;
            }
            n += count;
        } while (!isclosecap(cs.cap));
    }
#if 0
    /* No longer return end pos as result, as match object handles this */
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
#endif

    return result;
}

/* **********************************************************************
 * Finally, the matcher
 * **********************************************************************
 */
static const char *match (const char *o, const char *s, const char *e,
                          PyObject *patt, Capture **capturep, PyObject *args) {
    Stack stackbase[MAXBACK];
    Stack *stacklimit = stackbase + MAXBACK;
    Stack *stack = stackbase;  /* point to first empty slot in stack */
    int capsize = IMAXCAPTURES;
    int captop = 0;  /* point to first empty slot in captures */
    const Instruction *op = patprog(patt);
    const Instruction *p = op;
    Capture *capture = *capturep;
    stack->p = &giveup; stack->s = s; stack->caplevel = 0; stack++;
#ifdef TRACE
    Py_XDECREF(((Pattern*)patt)->trace);
    ((Pattern*)patt)->trace = PyList_New(0);
#endif
    for (;;) {
#if defined(DEBUG)
        printf("s: |%s| stck: %d c: %d  ", s, stack - stackbase, captop);
        printinst(op, p);
#endif
#ifdef TRACE
        if (((Pattern*)patt)->trace)
        {
            PyObject *elem = Py_BuildValue("nn", (p-op), (s-o));
            if (elem)
                PyList_Append(((Pattern*)patt)->trace, elem);
        }
#endif
        switch ((Opcode)p->i.code) {
            case IEnd: {
                if (stack != stackbase + 1) {
                  PyErr_SetString(PyExc_RuntimeError, "Pattern end with unbalanced stack");
                  return NULL;
                }
                capture[captop].kind = Cclose;
                capture[captop].s = NULL;
                return s;
            }
            case IGiveup: {
                if (stack != stackbase) {
                  PyErr_SetString(PyExc_RuntimeError, "Giveup instruction found in pattern");
                  return NULL;
                }
                /* Make sure we don't have a pending error */
                PyErr_Clear();
                return NULL;
            }
            case IRet: {
                if (stack <= stackbase || (stack - 1)->s != NULL) {
                  PyErr_SetString(PyExc_RuntimeError, "Unbalanced call/return opcodes");
                  return NULL;
                }
                p = (--stack)->p;
                continue;
            }
            case IAny: {
                int n = p->i.aux;
                if (n <= e - s) { p++; s += n; }
                else condfailed(p);
                continue;
            }
            case IChar: {
                if ((byte)*s == p->i.aux && s < e) { p++; s++; }
                else condfailed(p);
                continue;
            }
            case ISet: {
                int c = (byte)*s;
                if (testchar((p+1)->buff, c) && s < e)
                    { p += CHARSETINSTSIZE; s++; }
                else condfailed(p);
                continue;
            }
            case ISpan: {
                for (; s < e; s++) {
                    int c = (byte)*s;
                    if (!testchar((p+1)->buff, c)) break;
                }
                p += CHARSETINSTSIZE;
                continue;
            }
            case IFunc: {
                const char *r = (p+1)->f((p+2)->buff, o, s, e);
                if (r == NULL) goto fail;
                s = r;
                p += p->i.offset;
                continue;
            }
            case IJmp: {
                p += p->i.offset;
                continue;
            }
            case IChoice: {
                if (stack >= stacklimit) {
                  PyErr_SetString(PyExc_RuntimeError, "Too many pending calls/choices");
                  return NULL;
                }
                stack->p = dest(0, p);
                stack->s = s - p->i.aux;
                stack->caplevel = captop;
                stack++;
                p++;
                continue;
            }
            case ICall: {
                if (stack >= stacklimit) {
                  PyErr_SetString(PyExc_RuntimeError, "Too many pending calls/choices");
                  return NULL;
                }
                stack->s = NULL;
                stack->p = p + 1;  /* save return address */
                stack++;
                p += p->i.offset;
                continue;
            }
            case ICommit: {
                if (stack <= stackbase || (stack - 1)->s == NULL) {
                  PyErr_SetString(PyExc_RuntimeError, "Unbalanced commit opcodes");
                  return NULL;
                }
                stack--;
                p += p->i.offset;
                continue;
            }
            case IPartialCommit: {
                if (stack <= stackbase || (stack - 1)->s == NULL) {
                  PyErr_SetString(PyExc_RuntimeError, "Unbalanced commit opcodes");
                  return NULL;
                }
                (stack - 1)->s = s;
                (stack - 1)->caplevel = captop;
                p += p->i.offset;
                continue;
            }
            case IBackCommit: {
                if (stack <= stackbase || (stack - 1)->s == NULL) {
                  PyErr_SetString(PyExc_RuntimeError, "Unbalanced commit opcodes");
                  return NULL;
                }
                s = (--stack)->s;
                p += p->i.offset;
                continue;
            }
            case IFailTwice:
                if (stack <= stackbase) {
                  PyErr_SetString(PyExc_RuntimeError, "Cannot fail: stack is empty");
                  return NULL;
                }
                stack--;
                /* go through */
            case IFail:
            fail: { /* pattern failed: try to backtrack */
                do {  /* remove pending calls */
                    if (stack <= stackbase) {
                      PyErr_SetString(PyExc_RuntimeError, "Cannot fail: stack is empty");
                      return NULL;
                    }
                    s = (--stack)->s;
                } while (s == NULL);
                captop = stack->caplevel;
                p = stack->p;
                continue;
            }
            case ICloseRunTime: {
                int fr = PyList_Size(patenv(patt));
                PyObject *result;
                PyObject *extravalues = NULL;
                long res;
                Py_ssize_t n = 0;
                int ncap = runtimecap(capture + captop, capture, o, s, patt, args, &result);
                if (ncap == -1 || result == NULL)
                    return NULL;
                if (PySequence_Check(result)) {
                    PyObject *rtresult = PySequence_ITEM(result, 0);
                    extravalues = PySequence_ITEM(result, 1);
                    Py_DECREF(result);
                    result = rtresult;
                    for (n = 0; n < PySequence_Size(extravalues); n++) {
                        PyObject *val = PySequence_ITEM(extravalues, n);
                        Py_ssize_t len = val2env(patt, val);
                        Py_DECREF(val);
                    }
                    Py_DECREF(extravalues);
                }
                if (result == Py_None || result == Py_False) {
                    Py_DECREF(result);
                    goto fail;
                }
                else if (result == Py_True) {
                    res = s - o;  /* keep current position */
                }
                else if (PyInt_Check(result)) {
                    res = PyInt_AsLong(result);
                }
                Py_DECREF(result);
                if (res == -1 && PyErr_Occurred())
                    return NULL;
                if (res < s - o || res > e - o) {
                    PyErr_SetString(PyExc_RuntimeError, "Invalid position returned by match-time capture");
                    return NULL;
                }
                s = o + res;  /* update current position */
                captop -= ncap;  /* remove nested captures */
                if (n > 0) {  /* captures? */
                    if ((captop += n + 1) >= capsize) {
                        capture = doublecap(capture, captop);
                        if (PyErr_Occurred())
                            return NULL;
                        *capturep = capture;
                        capsize = 2 * captop;
                    }
                    // FIXME Why is it fr+1, not fr like in lpeg.c?
                    adddyncaptures(s, capture + captop - n - 1, n, fr+1);
                }
                p++;
                continue;
            }
            case ICloseCapture: {
                const char *s1 = s - getoff(p);
                if (captop <= 0) {
                    PyErr_SetString(PyExc_RuntimeError, "Close capture with no pending captures");
                    return NULL;
                }
                if (capture[captop - 1].siz == 0 &&
                        s1 - capture[captop - 1].s < UCHAR_MAX) {
                    capture[captop - 1].siz = s1 - capture[captop - 1].s + 1;
                    p++;
                    continue;
                }
                else {
                    capture[captop].siz = 1;  /* mark entry as closed */
                    goto capture;
                }
            }
            case IEmptyCapture: case IEmptyCaptureIdx:
                capture[captop].siz = 1;  /* mark entry as closed */
                goto capture;
            case IOpenCapture:
                capture[captop].siz = 0;  /* mark entry as open */
                goto capture;
            case IFullCapture:
                capture[captop].siz = getoff(p) + 1;  /* save capture size */
            capture: {
                capture[captop].s = s - getoff(p);
                capture[captop].idx = p->i.offset;
                capture[captop].kind = getkind(p);
                if (++captop >= capsize) {
                    capture = doublecap(capture, captop);
                    if (PyErr_Occurred())
                        return NULL;
                    *capturep = capture;
                    capsize = 2 * captop;
                }
                p++;
                continue;
            }
            case IOpenCall: {
                PyErr_SetString(PyExc_RuntimeError, "Reference to rule outside a grammar");
                return NULL;
            }
            default:
                PyErr_SetString(PyExc_RuntimeError, "Unknown opcode");
                return NULL;
        }
    }
}
static PyObject *
Pattern_call(PyObject *self, PyObject *args, PyObject *kw)
{
    char *str;
    Py_ssize_t len;
    Capture *cc;
    const char *e;
    PyObject *result;
    Match *res;

#if 0
    if (!PyArg_ParseTuple(args, "s#", &str, &len))
        return NULL;
#else
    PyObject *target = PyTuple_GetItem(args, 0);
    if (target == NULL || PyString_AsStringAndSize(target, &str, &len) == -1)
        return NULL;
#endif

    result = PyObject_CallFunction(match_cls, "");
    if (result == NULL)
        return NULL;

    cc = malloc(IMAXCAPTURES * sizeof(Capture));
    res = (Match *)result;
    e = match(str, str, str + len, self, &cc, args);
    if (e == 0) {
        free(cc);
        if (PyErr_Occurred()) {
            Py_DECREF(result);
            return NULL;
        }
        return result;
    }
    res->pos = e - str;
    res->captures = getcaptures((PyObject*)self, &cc, str, e, args);
    free(cc);
    if (res->captures == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

/* **********************************************************************
 * Module creation - type initialisation, method tables, etc
 * **********************************************************************
 */
#ifdef TRACE
static PyMemberDef Pattern_members[] = {
    {"trace", T_OBJECT, offsetof(Pattern, trace), READONLY},
    {0}
};
#endif

static PyMethodDef Pattern_methods[] = {
    {"dump", (PyCFunction)Pattern_dump, METH_NOARGS,
     "Build a list representing the pattern, for debugging"
    },
    {"display", (PyCFunction)Pattern_display, METH_NOARGS,
     "Print the pattern, for debugging"
    },
    {"_set_code", (PyCFunction)Pattern_set_code, METH_VARARGS,
     "Set the code and environ for the pattern (internal use)"
    },
    {"env", (PyCFunction)Pattern_env, METH_NOARGS,
     "The pattern environment, for debugging"
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
    {"CapC", (PyCFunction)Pattern_CaptureConst, METH_VARARGS | METH_CLASS,
     "A constant capture"
    },
    {"CapB", (PyCFunction)Pattern_CaptureBack, METH_O | METH_CLASS,
     "A back reference"
    },
    {"CapG", (PyCFunction)Pattern_CaptureGroup, METH_VARARGS | METH_CLASS,
     "A group capture"
    },
    {"CapF", (PyCFunction)Pattern_CaptureFold, METH_VARARGS | METH_CLASS,
     "A fold capture"
    },
    {"CapRT", (PyCFunction)Pattern_CaptureRuntime, METH_VARARGS | METH_CLASS,
     "A runtime capture"
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
    (binaryfunc)Pattern_divide, /* binaryfunc nb_divide */
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
    (binaryfunc)Pattern_or, /* binaryfunc nb_or */
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
    (richcmpfunc)Pattern_richcompare,
                               /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Pattern_methods,           /* tp_methods */
#ifdef TRACE
    Pattern_members,           /* tp_members */
#else
    0,                         /* tp_members */
#endif
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

static PyNumberMethods Match_as_number = {
    0, /* binaryfunc nb_add */
    0,   /* binaryfunc nb_subtract */
    0, /* binaryfunc nb_multiply */
    0, /* binaryfunc nb_divide */
    0, /* binaryfunc nb_remainder */
    0, /* binaryfunc nb_divmod */
    0, /* ternaryfunc nb_power */
    0, /* unaryfunc nb_negative */
    0, /* unaryfunc nb_positive */
    0, /* unaryfunc nb_absolute */
    (inquiry)Match_nonzero, /* inquiry nb_nonzero */
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

static PyMemberDef Match_members[] = {
    {"pos", T_LONG, offsetof(Match, pos), READONLY},
    {"captures", T_OBJECT, offsetof(Match, captures), READONLY},
    {0}
};

static PyTypeObject MatchType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /* ob_size */
    "_ppeg.Match",             /* tp_name */
    sizeof(Match),             /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)Match_dealloc,
                               /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    0,                         /* tp_repr */
    &Match_as_number,          /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
                               /* tp_flags*/
    "Match object",            /* tp_doc */
    (traverseproc)Match_traverse,
                               /* tp_traverse */
    (inquiry)Match_clear,      /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                         /* tp_methods */
    Match_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    Match_new,                 /* tp_new */
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

    if (PyType_Ready(&MatchType) < 0)
        return;

    m = Py_InitModule3("_ppeg", _ppeg_methods, "PEG parser module.");
    if (m == NULL)
        return;

    Py_INCREF(&PatternType);
    PyModule_AddObject(m, "Pattern", pattern_cls);
}
