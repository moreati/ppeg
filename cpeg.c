#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

typedef enum {
    iEnd = 0,
    iChar,
    iJump,
    iChoice,
    iCall,
    iReturn,
    iCommit,
    iCapture,
    iFail,

    /* Extended Codes */
    iAny,
    iCharset,
    iPartialCommit,
    iSpan,
    iFailTwice,
    iBackCommit,
    iTestChar,
    iTestCharset,
    iTestAny,

    /* Non-executable instructions */
    iOpenCall,
} InstructionCode;

char *instruction_names[] = {
    "End",
    "Char",
    "Jump",
    "Choice",
    "Call",
    "Return",
    "Commit",
    "Capture",
    "Fail",
    "Any",
    "Charset",
    "PartialCommit",
    "Span",
    "FailTwice",
    "BackCommit",
    "TestChar",
    "TestCharset",
    "TestAny",
    "OpenCall",
};

#define CHARSET_BYTES ((UCHAR_MAX/CHAR_BIT) + 1)
typedef unsigned char Charset[CHARSET_BYTES];
#define IN_CHARSET(set, ch) ((int)(set)[(ch)>>3] & (1 << ((ch)&7)))
#define SET_CHARSET(set, ch) ((set)[(ch)>>3] |= (1 << ((ch)&7)))

typedef struct {
    InstructionCode instr;
    signed int offset;
    union {
	unsigned int count;
	char character;
	Charset cset;
	void *capture_info;
	unsigned int rule;
    };
} Instruction;

typedef struct stackentry {
    enum { ReturnData, BacktrackData } entry_type;
    union {
	unsigned int ret;
	struct {
	    unsigned int alternative;
	    unsigned int pos;
	    void *capture_info;
	};
    };
} StackEntry;

static StackEntry *stack = NULL;
static int stackpos = -1;
static int stacksize = 0;

#define STACK_CHUNK (100)
#define STACK_PUSHRET(rtn) (Stack_Ensure(), (++stackpos), \
	(stack[stackpos].entry_type = ReturnData), \
	(stack[stackpos].ret = (rtn)))
#define STACK_PUSHALT(alt, psn, cap) (Stack_Ensure(), (++stackpos), \
	(stack[stackpos].entry_type = BacktrackData), \
	(stack[stackpos].alternative = (alt)), \
	(stack[stackpos].pos = (psn)), \
	(stack[stackpos].capture_info = (cap)))
#define STACK_EMPTY() (stackpos == -1)
#define STACK_TOPTYPE() (stack[stackpos].entry_type)
#define STACK_POP() (--stackpos)
#define STACK_TOP() (stack[stackpos])

void Stack_Ensure (void) {
    /* At start, stack == NULL, stacksize == 0, stackpos == -1, so we allocate
     * first time
     */
    if (stackpos + 1 >= stacksize) {
	stacksize += STACK_CHUNK;
	stack = realloc(stack, stacksize * sizeof(StackEntry));
    }
}

#define FAIL ((unsigned int)(-1))

/* Returns the index of the first unmatched character, or -1 if the match
 * failed
 */
int run (Instruction *prog, const char *target)
{
    unsigned int pc = 0;
    unsigned int pos = 0;
    void *capture = NULL;
    size_t target_len = strlen(target);
    Instruction *instr;

    for (;;) {
	if (pc == FAIL) {
	    /* Machine is in fail state */
	    if (STACK_EMPTY()) {
		/* No further options */
		return -1;
	    }
	    if (STACK_TOPTYPE() == BacktrackData) {
		/* Backtrack to stacked alternative */
		pc = STACK_TOP().alternative;
		pos = STACK_TOP().pos;
		capture = STACK_TOP().capture_info;
	    }
	    /* Pop one stack entry */
	    STACK_POP();
	    continue;
	}
	instr = &prog[pc];
#if 0
	printf("pc = %d, pos = %d, instr = %s\n", pc, pos, 
		instruction_names[instr->instr]);
#endif
	switch (instr->instr) {
            case iEnd:
		return pos;
            case iChar:
		if (target[pos] == instr->character) {
		    pos += 1;
		    pc += 1;
		} else {
		    pc = FAIL;
		}
		break;
            case iJump:
		pc += instr->offset;
		break;
            case iChoice:
		STACK_PUSHALT(pc + instr->offset, pos - instr->count, capture);
		pc += 1;
		break;
            case iCall:
		STACK_PUSHRET(pc + 1);
		pc += instr->offset;
		break;
            case iReturn:
		pc = STACK_TOP().ret;
		STACK_POP();
		break;
            case iCommit:
		STACK_POP();
		pc += instr->offset;
		break;
            case iCapture:
		/* TODO: Add capture info */
		pc += 1;
		break;
            case iFail:
		pc = FAIL;
		break;
            case iAny:
		if (pos + instr->count <= target_len) {
		    pc += 1;
		    pos += instr->count;
		} else {
		    pc = FAIL;
		}
		break;
            case iCharset:
		if (IN_CHARSET(instr->cset, target[pos])) {
		    pc += 1;
		    pos += 1;
		} else {
		    pc = FAIL;
		}
                break;
            case iPartialCommit:
		if (STACK_TOPTYPE() == BacktrackData) {
		    /* Replace the backtrack data on the top of the stack */
		    STACK_TOP().pos = pos;
		    STACK_TOP().capture_info = capture;
		} else {
		    /* Cannot happen */
		    assert(0);
		}
		pc += instr->offset;
                break;
            case iSpan:
		if (IN_CHARSET(instr->cset, target[pos]))
		    pos += 1;
		else
		    pc += 1;
                break;
            case iFailTwice:
		STACK_POP();
		pc = FAIL;
                break;
            case iBackCommit:
		if (STACK_TOPTYPE() == BacktrackData) {
		    /* Pop the position and capture info, but jump */
		    pos = STACK_TOP().pos;
		    capture = STACK_TOP().capture_info;
		    STACK_POP();
		} else {
		    /* Cannot happen */
		    assert(0);
		}
		pc += instr->offset;
                break;
            case iTestChar:
		if (target[pos] == instr->character) {
		    pos += 1;
		    pc += 1;
		} else {
		    pc += instr->offset;
		}
                break;
            case iTestCharset:
		if (IN_CHARSET(instr->cset, target[pos])) {
		    pos += 1;
		    pc += 1;
		} else {
		    pc += instr->offset;
		}
                break;
            case iTestAny:
		if (pos + instr->count <= target_len) {
		    pos += instr->count;
		    pc += 1;
		} else {
		    pc += instr->offset;
		}
                break;
	    default:
		/* Cannot happen - skip the instruction */
		pc += 1;
		break;
	}
    }

    /* We never actually reach here - we exit at the End instruction */
    return pos;
}

Instruction *Match(char *string)
{
    size_t len = strlen(string);
    Instruction *prog = malloc((len + 1) * sizeof(Instruction));
    char *p;
    Instruction *instr;

    /* Minimalist error handling */
    if (prog == NULL)
	return prog;

    for (p = string, instr = prog; *p; ++p, ++instr) {
	instr->instr = iChar;
	instr->character = *p;
    }
    instr->instr = iEnd;

    return prog;
}

Instruction *Any(unsigned int n)
{
    Instruction *prog = malloc(2 * sizeof(Instruction));

    /* Minimalist error handling */
    if (prog == NULL)
	return prog;

    prog[0].instr = iAny;
    prog[0].count = n;
    prog[1].instr = iEnd;

    return prog;
}

Instruction *Concat (Instruction *p, Instruction *q)
{
    Instruction *prog;
    int i;
    int pc;
    size_t plen;
    size_t qlen;

    /* Count instructions */
    for (plen = 0; p[plen].instr != iEnd; ++plen)
	;
    for (qlen = 0; q[qlen].instr != iEnd; ++qlen)
	;
    prog = malloc((plen + qlen + 1) * sizeof(Instruction));
    if (prog == 0)
	return prog;
    pc = 0;
    for (i = 0; i < plen; ++i, ++pc)
	prog[pc] = p[i];
    for (i = 0; i < qlen; ++i, ++pc)
	prog[pc] = q[i];
    prog[pc].instr = iEnd;

    /* TODO: Better memory management */
    free(p);
    free(q);
    return prog;
}

Instruction *Choose (Instruction *p, Instruction *q)
{
    Instruction *prog;
    int i;
    int pc;
    size_t plen;
    size_t qlen;

    /* Count instructions */
    for (plen = 0; p[plen].instr != iEnd; ++plen)
	;
    for (qlen = 0; q[qlen].instr != iEnd; ++qlen)
	;
    /* Choice L1
     * p
     * Commit L2
     * L1: q
     * L2:
     */
    prog = malloc((plen + qlen + 3) * sizeof(Instruction));
    if (prog == 0)
	return prog;
    pc = 0;
    prog[pc].instr = iChoice;
    prog[pc].offset = plen + 2;
    prog[pc].count = 0;
    ++pc;
    for (i = 0; i < plen; ++i, ++pc)
	prog[pc] = p[i];
    prog[pc].instr = iCommit;
    prog[pc].offset = qlen + 1;
    ++pc;
    for (i = 0; i < qlen; ++i, ++pc)
	prog[pc] = q[i];
    prog[pc].instr = iEnd;

    /* PEEPHOLE: Initial Choice/Char changed to TestChar/Choice */
    if (prog[1].instr == iChar) {
	char ch = prog[1].character;
	int offset = prog[0].offset;
	prog[0].instr = iTestChar;
	prog[0].character = ch;
	prog[1].instr = iChoice;
	prog[1].offset = offset - 1;
	prog[1].count = 1;
    }
    /* TODO: Better memory management */
    free(p);
    free(q);
    return prog;
}

Instruction *Not (Instruction *p)
{
    Instruction *prog;
    int i;
    int pc;
    size_t plen;

    /* Count instructions */
    for (plen = 0; p[plen].instr != iEnd; ++plen)
	;
    /* Choice L2
     * p
     * Commit L1
     * L1: Fail
     * L2:
     */
    prog = malloc((plen + 4) * sizeof(Instruction));
    if (prog == 0)
	return prog;
    pc = 0;
    prog[pc].instr = iChoice;
    prog[pc].offset = plen + 3;
    prog[pc].count = 0;
    ++pc;
    for (i = 0; i < plen; ++i, ++pc)
	prog[pc] = p[i];
    prog[pc].instr = iCommit;
    prog[pc].offset = 1;
    ++pc;
    prog[pc].instr = iFail;
    ++pc;
    prog[pc].instr = iEnd;

    /* TODO: Better memory management */
    free(p);
    return prog;
}

Instruction *Repeat (Instruction *p)
{
    Instruction *prog;
    int i;
    int pc;
    size_t plen;

    /* Count instructions */
    for (plen = 0; p[plen].instr != iEnd; ++plen)
	;
    /* L1: Choice L2
     * p
     * Commit L1
     * L2:
     */
    prog = malloc((plen + 3) * sizeof(Instruction));
    if (prog == 0)
	return prog;
    pc = 0;
    prog[pc].instr = iChoice;
    prog[pc].offset = plen + 2;
    prog[pc].count = 0;
    ++pc;
    for (i = 0; i < plen; ++i, ++pc)
	prog[pc] = p[i];
    prog[pc].instr = iCommit;
    prog[pc].offset = - (plen + 1);
    ++pc;
    prog[pc].instr = iEnd;

    /* TODO: Better memory management */
    free(p);
    return prog;
}

/* Temporary option until variable references are implemented.
 *
 * Implements Choose(p, Concat(Any(1), p))
 * but with the second p as a call to the original pattern.
 */
Instruction *Anywhere (Instruction *p)
{
    Instruction *prog;
    int i;
    int pc;
    size_t plen;

    /* Count instructions */
    for (plen = 0; p[plen].instr != iEnd; ++plen)
	;
    /* L0: Choice L1
     * p
     * Commit L2
     * L1: Any 1
     * Jump L0
     * L2:
     */
    prog = malloc((plen + 5) * sizeof(Instruction));
    if (prog == 0)
	return prog;
    pc = 0;
    prog[pc].instr = iChoice;
    prog[pc].offset = plen + 2;
    prog[pc].count = 0;
    ++pc;
    for (i = 0; i < plen; ++i, ++pc)
	prog[pc] = p[i];
    prog[pc].instr = iCommit;
    prog[pc].offset = 3;
    ++pc;
    prog[pc].instr = iAny;
    prog[pc].count = 1;
    ++pc;
    prog[pc].instr = iJump;
    prog[pc].offset = - (plen + 3);
    ++pc;
    prog[pc].instr = iEnd;

    /* TODO: Better memory management */
    free(p);
    return prog;
}

Instruction *Call (int rule)
{
    Instruction *prog = malloc(2 * sizeof(Instruction));
    prog[0].instr = iOpenCall;
    prog[0].rule = rule;
    prog[1].instr = iEnd;
    return prog;
}

typedef Instruction *pins;
Instruction *Grammar (Instruction *rule, ...)
{
    int ruleno = 0;
    va_list ap;
    Instruction *prog = 0;
    int *labels = 0;
    size_t prog_len = 0;
    size_t labels_len = 0;
    int pc = 2; /* Leave 2 initial spaces */
    int rlen;
    int r;
    int p;
    int offset;

    va_start(ap, rule);

    while (rule) {
	++ruleno;
	if (ruleno >= labels_len) {
	    labels_len += 10;
	    labels = realloc(labels, labels_len * sizeof(int));
	    labels[ruleno-1] = pc;
	}
	/* Count instructions */
	for (rlen = 0; rule[rlen].instr != iEnd; ++rlen)
	    ;
	if ((pc + rlen + 1) > prog_len) {
	    prog_len = pc + rlen + 1 + 10;
	    prog = realloc(prog, prog_len * sizeof(Instruction));
	}
	for (r = 0; r < rlen; ++r, ++pc)
	    prog[pc] = rule[r];
	prog[pc++].instr = iReturn;

	/* TODO: Better memory management!!! */
	free(rule);
	rule = va_arg(ap, pins);
    }

    if ((pc + 1) > prog_len) {
	prog_len += 10;
	prog = realloc(prog, prog_len * sizeof(Instruction));
    }

    prog[pc++].instr = iEnd;
    va_end(ap);

    /* Now fix up OpenCall instructions */
    for (p = 0; p < pc; ++p) {
	if (prog[p].instr != iOpenCall)
	    continue;
	r = prog[p].rule;
	offset = labels[r-1] - p;
	/* Tail call optimisation */
	if (prog[p+1].instr == iReturn)
	    prog[p].instr = iJump;
	else
	    prog[p].instr = iCall;
	prog[p].offset = offset;
    }

    /* Now fill in the inintal "Call startrule, Jump end" sequence */
    prog[0].instr = iCall;
    prog[0].offset = 2;
    prog[1].instr = iJump;
    prog[1].offset = pc - 2;

    free(labels);
    return prog;
}

void Dump (Instruction *p)
{
    while (p->instr != iEnd) {
	printf("%s(", instruction_names[p->instr]);
	switch (p->instr) {
            case iReturn:
            case iCapture:
            case iFail:
            case iFailTwice:
            case iEnd:
		break;
            case iChar:
		printf("'%c'", p->character);
		break;
            case iJump:
            case iCall:
            case iCommit:
            case iPartialCommit:
            case iBackCommit:
		printf("%d", p->offset);
		break;
            case iChoice:
		printf("%d, %d", p->offset, p->count);
		break;
            case iAny:
		printf ("%d", p->count);
		break;
            case iCharset:
            case iSpan:
		// p->cset
                break;
            case iTestChar:
		printf("'%c', %d", p->character, p->offset);
                break;
            case iTestCharset:
		// printf("'%c', %d", p->cset, p->offset);
                break;
            case iTestAny:
		printf("%d, %d", p->count, p->offset);
                break;
	    default:
		break;
	}
	printf(")\n");
	++p;
    }
}
