from operator import add, div, mul, neg

from _ppeg import Pattern as P

def pattprint(pattern):
    print pattern.env()
    pattern.display()

mt = P(1)
ANY = P(1)

predef = {
    'nl': P('\n'),
}

def getdef(name, defs):
    c = defs and defs[name]
    return c

def patt_error(s, i):
    msg = (len(s) < i + 20) and s[i] or s[i:i+20] + '...'
    msg = "pattern error near '%s'" % (msg, )
    raise Exception(msg)

def mult(p, n):
    """Returns a Pattern that matches exactly n repetitions of Pattern p.
    """
    np = P()
    while n >= 1:
        if n % 2:
            np = np + p
        p = p + p
        n = n // 2
    return np

def equalcap(s, i, (c,)):
    if not isinstance(c, str):
        return None
    e = len(c) + i
    if s[i: e] == c:
        return e
    else:
        return None

S = (P.Set(" \t\n") | '--' + (ANY - P.Set("\n"))**0)**0

name = P.Range("AZaz") + P.Range("AZaz09")**0
exp_follow = P('/') | P(")") | P("}") | P(":}") | P("~}") | name | -1
name = P.Cap(name)

Identifier = name + P.CapA(1)

num = (P.Cap(P.Range("09")**1) + S) / int

String = (("'" + P.Cap((ANY - "'")**0) + "'") |
          ('"' + P.Cap((ANY - '"')**0) + '"'))


def getcat(c, defs):
    cat = defs.get(c, predef.get(c))
    if not cat:
        raise Exception('name %s undefined' % (c,))
    return cat

Cat = ('%' + Identifier) / getcat

Range   = P.CapS(ANY + (P("-")/"") + (ANY-"]")) / P.Range

item    = Cat | Range | P.Cap(ANY)


def f(c, p):
    if c == "^":
        return ANY - p
    else:
        return p

Class   = ( ("[" + P.Cap(P("^")**-1) # optional complement symbol
                 + P.CapF(item + (item - "]")**0, mt.__or__)) / f
           ) + "]"

def adddef(d, k, defs, exp):
    if d.get(k):
        raise Exception("'%s' already defined as a rule" % k)
    d[k] = exp
    return d

def firstdef(n, defs, r):
    return adddef({}, k, defs, r)

def abf(a, b, f):
    return f(a, b)

def np(n, p):
    return P.CapG(p, n)

exp = P.Grammar(
    # 0 Exp
    (S + ( P.Var(6)
        | P.CapF(P.Var(1) + ('/' + S + P.Var(1))**0, mt.__or__) )),

    # 1 Seq
    (P.CapF(P.CapC(P("")) + P.Var(2)**0, mt.__add__)
    + (+exp_follow | patt_error)),

    # 2 Prefix 
    ( ("&" + S + P.Var(2)) / mt.__pos__
    | ("!" + S + P.Var(2)) / mt.__neg__
    | P.Var(3)),

    # 3 Suffix
    (P.CapF(P.Var(4) + S +
           ( ( P("+") + P.CapC(1, mt.__pow__)
             | P("*") + P.CapC(0, mt.__pow__)
             | P("?") + P.CapC(-1, mt.__pow__)
             | "^" + ( P.CapG(num + P.CapC(mult))
                     | P.CapG(P.Cap(P.Set("+-") + P.Range("09")**1)
                              + P.CapC(mt.__pow__))
                     )
             | "->" + S + ( P.CapG(String + P.CapC(mt.__div__))
                          | P("{}") + P.CapC(None, P.CapT)
                          | P.CapG(Identifier / getdef + P.CapC(mt.__div__))
                          )
             | "=>" + S + P.CapG(Identifier / getdef + P.CapC(P.CapRT))
             ) + S
            )**0, abf)),

    # 4 Primary
    ("(" + P.Var(0) + ")"
    | String / P
    | Class
    | Cat
    | ("{:" + (name + ":" | P.CapC(None)) + P.Var(0) + ":}") / np
    | ("=" + name) / (lambda n: CapRT(CapB(n), equalcap))
    | P("{}") / P.CapP
    | ("{~" + P.Var(0) + "~}") / P.CapS
    | ("{"  + P.Var(0) +  "}") / P.Cap
    | P(".") + P.CapC(ANY)
    | ("<" + name + ">") / P.Var),
    # 5 Definition
    (Identifier + S + '<-' + P.Var(0)),
    # 6 Grammar
    (P.CapF(P.Var(5) / firstdef + P.CapG(P.Var(5))**0, adddef) / P.Grammar),
)
#pattprint(exp)
pattern = (S + exp) / P + (-ANY | patt_error)


def compile(p, defs=None):
    m = pattern(p, defs)
    return m.captures[0]

balanced = compile('balanced <- "(" ([^()] / <balanced>)* ")"')
if __name__ == '__main__':
    print '(hello())', balanced('(hello())').pos
