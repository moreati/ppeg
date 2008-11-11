#include <Python.h>

/* **********************************************************************
 * Lua cpeg code, copied here with minimal changes
 * **********************************************************************/

/*
** $Id: lpeg.c,v 1.98 2008/10/11 20:20:43 roberto Exp $
** LPeg - PEG pattern matching for Lua
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
** written by Roberto Ierusalimschy
*/


#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>


#define VERSION		"0.9"
#define PATTERN_T	"pattern"

/* maximum call/backtrack levels */
#define MAXBACK		400

/* initial size for capture's list */
#define IMAXCAPTURES	600


/* index, on Lua stack, for subject */
#define SUBJIDX		2

/* number of fixed arguments to 'match' (before capture arguments) */
#define FIXEDARGS	3

/* index, on Lua stack, for substitution value cache */
#define subscache(cs)	((cs)->ptop + 1)

/* index, on Lua stack, for capture list */
#define caplistidx(ptop)	((ptop) + 2)

/* index, on Lua stack, for pattern's fenv */
#define penvidx(ptop)	((ptop) + 3)



typedef unsigned char byte;


#define CHARSETSIZE		((UCHAR_MAX/CHAR_BIT) + 1)


typedef byte Charset[CHARSETSIZE];


typedef const char *(*PattFunc) (const void *ud,
                                 const char *o,  /* string start */
                                 const char *s,  /* current position */
                                 const char *e); /* string end */


/* Virtual Machine's instructions */
typedef enum Opcode {
  IAny, IChar, ISet, ISpan,
  IRet, IEnd,
  IChoice, IJmp, ICall, IOpenCall,
  ICommit, IPartialCommit, IBackCommit, IFailTwice, IFail, IGiveup,
  IFunc,
  IFullCapture, IEmptyCapture, IEmptyCaptureIdx,
  IOpenCapture, ICloseCapture, ICloseRunTime
} Opcode;


#define ISJMP		1
#define ISCHECK		(ISJMP << 1)
#define ISNOFAIL	(ISCHECK << 1)
#define ISCAPTURE	(ISNOFAIL << 1)
#define ISMOVABLE	(ISCAPTURE << 1)
#define ISFENVOFF	(ISMOVABLE << 1)
#define HASCHARSET	(ISFENVOFF << 1)

static const byte opproperties[] = {
  /* IAny */		ISCHECK,
  /* IChar */		ISCHECK,
  /* ISet */		ISCHECK | HASCHARSET,
  /* ISpan */		ISNOFAIL | HASCHARSET,
  /* IRet */		0,
  /* IEnd */		0,
  /* IChoice */		ISJMP,
  /* IJmp */		ISJMP | ISNOFAIL,
  /* ICall */		ISJMP,
  /* IOpenCall */	ISFENVOFF,
  /* ICommit */		ISJMP,
  /* IPartialCommit */	ISJMP,
  /* IBackCommit */	ISJMP,
  /* IFailTwice */	0,
  /* IFail */		0,
  /* IGiveup */		0,
  /* IFunc */		0,
  /* IFullCapture */	ISCAPTURE | ISNOFAIL | ISFENVOFF,
  /* IEmptyCapture */	ISCAPTURE | ISNOFAIL | ISMOVABLE,
  /* IEmptyCaptureIdx */ISCAPTURE | ISNOFAIL | ISMOVABLE | ISFENVOFF,
  /* IOpenCapture */	ISCAPTURE | ISNOFAIL | ISMOVABLE | ISFENVOFF,
  /* ICloseCapture */	ISCAPTURE | ISNOFAIL | ISMOVABLE | ISFENVOFF,
  /* ICloseRunTime */	ISCAPTURE | ISFENVOFF
};


typedef union Instruction {
  struct Inst {
    byte code;
    byte aux;
    short offset;
  } i;
  PattFunc f;
  byte buff[1];
} Instruction;

static const Instruction giveup = {{IGiveup, 0, 0}};

#define getkind(op)	((op)->i.aux & 0xF)
#define getoff(op)	(((op)->i.aux >> 4) & 0xF)

#define dest(p,x)	((x) + ((p)+(x))->i.offset)

#define MAXOFF		0xF

#define isprop(op,p)	(opproperties[(op)->i.code] & (p))
#define isjmp(op)	isprop(op, ISJMP)
#define iscapture(op) 	isprop(op, ISCAPTURE)
#define ischeck(op)	(isprop(op, ISCHECK) && (op)->i.offset == 0)
#define istest(op)	(isprop(op, ISCHECK) && (op)->i.offset != 0)
#define isnofail(op)	isprop(op, ISNOFAIL)
#define ismovable(op)	isprop(op, ISMOVABLE)
#define isfenvoff(op)	isprop(op, ISFENVOFF)
#define hascharset(op)	isprop(op, HASCHARSET)


/* kinds of captures */
typedef enum CapKind {
  Cclose, Cposition, Cconst, Cbackref, Carg, Csimple, Ctable, Cfunction,
  Cquery, Cstring, Csubst, Cfold, Cruntime, Cgroup
} CapKind;

#define iscapnosize(k)	((k) == Cposition || (k) == Cconst)


typedef struct Capture {
  const char *s;  /* position */
  short idx;
  byte kind;
  byte siz;
} Capture;


/* maximum size (in elements) for a pattern */
#define MAXPATTSIZE	(SHRT_MAX - 10)


/* size (in elements) for an instruction plus extra l bytes */
#define instsize(l)	(((l) - 1)/sizeof(Instruction) + 2)


/* size (in elements) for a ISet instruction */
#define CHARSETINSTSIZE		instsize(CHARSETSIZE)



#define loopset(v,b)	{ int v; for (v = 0; v < CHARSETSIZE; v++) b; }


#define testchar(st,c)	(((int)(st)[((c) >> 3)] & (1 << ((c) & 7))))
#define setchar(st,c)	((st)[(c) >> 3] |= (1 << ((c) & 7)))



static int sizei (const Instruction *i) {
  if (hascharset(i)) return CHARSETINSTSIZE;
  else if (i->i.code == IFunc) return i->i.offset;
  else return 1;
}


/*
** {======================================================
** Printing patterns
** =======================================================
*/


static void printcharset (const Charset st) {
  int i;
  PySys_WriteStdout("[");
  for (i = 0; i <= UCHAR_MAX; i++) {
    int first = i;
    while (testchar(st, i) && i <= UCHAR_MAX) i++;
    if (i - 1 == first)  /* unary range? */
      PySys_WriteStdout("(%02x)", first);
    else if (i - 1 > first)  /* non-empty range? */
      PySys_WriteStdout("(%02x-%02x)", first, i - 1);
  }
  PySys_WriteStdout("]");
}


static void printcapkind (int kind) {
  const char *const modes[] = {
    "close", "position", "constant", "backref",
    "argument", "simple", "table", "function",
    "query", "string", "substitution", "fold",
    "runtime", "group"};
  PySys_WriteStdout("%s", modes[kind]);
}


static void printjmp (const Instruction *op, const Instruction *p) {
  PySys_WriteStdout("-> ");
  if (p->i.offset == 0) PySys_WriteStdout("FAIL");
  else PySys_WriteStdout("%d", (int)(dest(0, p) - op));
}


static void printinst (const Instruction *op, const Instruction *p) {
  const char *const names[] = {
    "any", "char", "set", "span",
    "ret", "end",
    "choice", "jmp", "call", "open_call",
    "commit", "partial_commit", "back_commit", "failtwice", "fail", "giveup",
     "func",
     "fullcapture", "emptycapture", "emptycaptureidx", "opencapture",
     "closecapture", "closeruntime"
  };
  PySys_WriteStdout("%02ld: %s ", (long)(p - op), names[p->i.code]);
  switch ((Opcode)p->i.code) {
    case IChar: {
      PySys_WriteStdout("'%c'", p->i.aux);
      printjmp(op, p);
      break;
    }
    case IAny: {
      PySys_WriteStdout("* %d", p->i.aux);
      printjmp(op, p);
      break;
    }
    case IFullCapture: case IOpenCapture:
    case IEmptyCapture: case IEmptyCaptureIdx:
    case ICloseCapture: case ICloseRunTime: {
      printcapkind(getkind(p));
      PySys_WriteStdout("(n = %d)  (off = %d)", getoff(p), p->i.offset);
      break;
    }
    case ISet: {
      printcharset((p+1)->buff);
      printjmp(op, p);
      break;
    }
    case ISpan: {
      printcharset((p+1)->buff);
      break;
    }
    case IOpenCall: {
      PySys_WriteStdout("-> %d", p->i.offset);
      break;
    }
    case IChoice: {
      printjmp(op, p);
      PySys_WriteStdout(" (%d)", p->i.aux);
      break;
    }
    case IJmp: case ICall: case ICommit:
    case IPartialCommit: case IBackCommit: {
      printjmp(op, p);
      break;
    }
    default: break;
  }
  PySys_WriteStdout("\n");
}


void printpatt (Instruction *p) {
  Instruction *op = p;
  for (;;) {
    printinst(op, p);
    if (p->i.code == IEnd) break;
    p += sizei(p);
  }
}


static void printcap (Capture *cap) {
  printcapkind(cap->kind);
  PySys_WriteStdout(" (idx: %d - size: %d) -> %p\n", cap->idx, cap->siz, cap->s);
}


static void printcaplist (Capture *cap) {
  for (; cap->s; cap++) printcap(cap);
}

/* }====================================================== */




/*
** {======================================================
** Virtual Machine
** =======================================================
*/


typedef struct Stack {
  const char *s;
  const Instruction *p;
  int caplevel;
} Stack;


static void adddyncaptures (const char *s, Capture *base, int n, int fd) {
  int i;
  assert(base[0].kind == Cruntime && base[0].siz == 0);
  base[0].idx = fd;  /* first returned capture */
  for (i = 1; i < n; i++) {  /* add extra captures */
    base[i].siz = 1;  /* mark it as closed */
    base[i].s = s;
    base[i].kind = Cruntime;
    base[i].idx = fd + i;  /* stack index */
  }
  base[n].kind = Cclose;  /* add closing entry */
  base[n].siz = 1;
  base[n].s = s;
}


#define condfailed(p)	{ int f = p->i.offset; if (f) p+=f; else goto fail; }


static const char *match (const char *o, const char *s, const char *e,
                          Instruction *op, Capture *capture, int ptop) {
  Stack stackbase[MAXBACK];
  Stack *stacklimit = stackbase + MAXBACK;
  Stack *stack = stackbase;  /* point to first empty slot in stack */
  int capsize = IMAXCAPTURES;
  int captop = 0;  /* point to first empty slot in captures */
  const Instruction *p = op;
  stack->p = &giveup; stack->s = s; stack->caplevel = 0; stack++;
  for (;;) {
#if defined(DEBUG)
      PySys_WriteStderr("s: |%s| stck: %d c: %d  ", s, stack - stackbase, captop);
      printinst(op, p);
#endif
    switch ((Opcode)p->i.code) {
      case IEnd: {
        assert(stack == stackbase + 1);
        capture[captop].kind = Cclose;
        capture[captop].s = NULL;
        return s;
      }
      case IGiveup: {
        assert(stack == stackbase);
        return NULL;
      }
      case IRet: {
        assert(stack > stackbase && (stack - 1)->s == NULL);
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
        if (stack >= stacklimit)
          return ((char *)0);
        stack->p = dest(0, p);
        stack->s = s - p->i.aux;
        stack->caplevel = captop;
        stack++;
        p++;
        continue;
      }
      case ICall: {
        if (stack >= stacklimit)
          return ((char *)0);
        stack->s = NULL;
        stack->p = p + 1;  /* save return address */
        stack++;
        p += p->i.offset;
        continue;
      }
      case ICommit: {
        assert(stack > stackbase && (stack - 1)->s != NULL);
        stack--;
        p += p->i.offset;
        continue;
      }
      case IPartialCommit: {
        assert(stack > stackbase && (stack - 1)->s != NULL);
        (stack - 1)->s = s;
        (stack - 1)->caplevel = captop;
        p += p->i.offset;
        continue;
      }
      case IBackCommit: {
        assert(stack > stackbase && (stack - 1)->s != NULL);
        s = (--stack)->s;
        p += p->i.offset;
        continue;
      }
      case IFailTwice:
        assert(stack > stackbase);
        stack--;
        /* go through */
      case IFail:
      fail: { /* pattern failed: try to backtrack */
        do {  /* remove pending calls */
          assert(stack > stackbase);
          s = (--stack)->s;
        } while (s == NULL);
        captop = stack->caplevel;
        p = stack->p;
        continue;
      }
#if 0
      case ICloseRunTime: {
        int fr = lua_gettop(L) + 1;  /* stack index of first result */
        int ncap = runtimecap(L, capture + captop, capture, o, s, ptop);
        lua_Integer res = lua_tointeger(L, fr) - 1;  /* offset */
        int n = lua_gettop(L) - fr;  /* number of new captures */
        if (res == -1) {  /* may not be a number */
          if (!lua_toboolean(L, fr)) {  /* false value? */
            lua_settop(L, fr - 1);  /* remove results */
            goto fail;  /* and fail */
          }
          else if (lua_isboolean(L, fr))  /* true? */
            res = s - o;  /* keep current position */
        }
        if (res < s - o || res > e - o)
          luaL_error(L, "invalid position returned by match-time capture");
        s = o + res;  /* update current position */
        captop -= ncap;  /* remove nested captures */
        lua_remove(L, fr);  /* remove first result (offset) */
        if (n > 0) {  /* captures? */
          if ((captop += n + 1) >= capsize) {
            capture = doublecap(L, capture, captop, ptop);
            capsize = 2 * captop;
          }
          adddyncaptures(s, capture + captop - n - 1, n, fr);
        }
        p++;
        continue;
      }
#endif
      case ICloseCapture: {
        const char *s1 = s - getoff(p);
        assert(captop > 0);
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
          /*capture = doublecap(L, capture, captop, ptop);*/
          capsize = 2 * captop;
        }
        p++;
        continue;
      }
#if 0
      case IOpenCall: {
        lua_rawgeti(L, penvidx(ptop), p->i.offset);
        luaL_error(L, "reference to %s outside a grammar", val2str(L, -1));
      }
#endif
      default: assert(0); return NULL;
    }
  }
}

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
    {{IEnd,0,0}},
};

const Instruction *prog(void) { return Dummy; }

int domatch(Instruction *p, char *s) {
    Capture cc[IMAXCAPTURES];
    const char *e = match("", s, s+strlen(s), p, cc, 0);
    return e-s;
}

/* **********************************************************************
 * End of Lua cpeg code
 * **********************************************************************/

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    Instruction *prog;
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
    if (self != NULL) {
        self->prog = PyMem_New(Instruction, 50);
        if (self->prog == NULL) {
            Py_DECREF(self);
            return NULL;
        }
	self->prog[0].i.code = IEnd;
    }

    return (PyObject *)self;
}

static int
Pattern_init(Pattern *self, PyObject *args, PyObject *kwds)
{
    return 0;
}

static PyObject *
Pattern_dump(Pattern* self)
{
    printpatt(self->prog);
    Py_RETURN_NONE;
}

static PyObject *
Pattern_setdummy(Pattern* self)
{
    memcpy(self->prog, Dummy, sizeof(Dummy));
    Py_RETURN_NONE;
}

static PyObject *
Pattern_match(Pattern* self, PyObject *arg)
{
    char *str;
    Py_ssize_t len;
    Capture cc[IMAXCAPTURES];
    const char *e;

    if (PyString_AsStringAndSize(arg, &str, &len) != 0)
	return NULL;

    e = match("", str, str + len, self->prog, cc, 0);
    return PyInt_FromLong(e - str);
}

static PyMethodDef Pattern_methods[] = {
    {"dump", (PyCFunction)Pattern_dump, METH_NOARGS,
     "Dump the pattern, for debugging"
    },
    {"setdummy", (PyCFunction)Pattern_setdummy, METH_NOARGS,
     "Set the pattern to the 'dummy' value"
    },
    {"match", (PyCFunction)Pattern_match, METH_O,
     "Match the pattern against the supplied string"
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
    0,                         /*tp_call*/
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
