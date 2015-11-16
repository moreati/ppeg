# -*- coding: utf-8 -*-
"""A port of the lpeg test suite to ppeg.

This is a direct port of as much of the lpeg test suite as makes sense for
the ppeg port of lpeg.
"""

from __future__ import print_function

import sys

import pytest

from _ppeg import Pattern as P


any_ = P(1)
space = P.Set(" \t\n")**0

def match(pattern, string):
    return pattern(string)


# General tests for the ppeg library
def test_type():
    assert type("alo") != P
    assert type(sys.stdin) != P
    assert type(P.Match("alo")) == P


# Tests for some basic optimizations
def test_basic_opt_choice():
    assert match(P.Fail() | P.Match("a"), "a").pos == 1
    assert match(P() | P.Match("a"), "a").pos == 0
    assert not match(P.Match("a") | P.Fail(), "b")
    assert match(P.Match("a") | P(), "b").pos == 0


def test_basic_opt_concat():
    assert not match(P.Fail() + P.Match("a"), "a")
    assert match(P() + P.Match("a"), "a").pos == 1
    assert not match(P.Match("a") + P.Fail(), "a")
    assert match(P.Match("a") + P(), "a").pos == 1


def test_basic_opt_assert():
    assert not match(+P.Fail() + P.Match("a"), "a")
    assert match(+P() + P.Match("a"), "a").pos == 1
    assert not match(P.Match("a") + +P.Fail(), "a")
    assert match(P.Match("a") + +P(), "a").pos == 1


# Locale tests not included yet


def test_match_any():
    assert P.Any(3)("aaaa")
    assert P.Any(4)("aaaa")
    assert not P.Any(5)("aaaa")
    assert P.Any(-3)("aa")
    assert not P.Any(-3)("aaa")
    assert not P.Any(-3)("aaaa")
    assert not P.Any(-4)("aaaa")
    assert P.Any(-5)("aaaa")


def test_match_match():
    assert P.Match("a")("alo").pos == 1
    assert P.Match("al")("alo").pos == 2
    assert not P.Match("alu")("alo")
    assert P()("").pos == 0


# Character sets
def cs2str(c):
    # d = c.dump()
    # assert len(d) == 2, "cs2str: pattern is not a charset"
    # if d[0][0] == 'fail':
    #     return ""
    # if d[0][0] == 'char':
    #     return chr(d[0][1])
    # assert d[0][0] == 'set', "cs2str: pattern is not a charset (" + d[0][0] + ")"
    # return d[0][3]
    return "".join(chr(ch) for ch in range(256) if c(chr(ch)))


def eqcharset(c1, c2):
    assert cs2str(c1) == cs2str(c2)


digit = P.Set("0123456789")
upper = P.Set("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
lower = P.Set("abcdefghijklmnopqrstuvwxyz")
letter = P.Set("") | upper | lower
alpha = letter | digit | P.Range("")


def test_charset():
    eqcharset(P.Set(""), P.Fail())
    eqcharset(upper, P.Range("AZ"))
    eqcharset(lower, P.Range("az"))
    eqcharset(upper | lower, P.Range("azAZ"))
    eqcharset(upper | lower, P.Range("AZczaabb90"))
    eqcharset(digit, P.Set("01234567") | P.Match("8") | P.Match("9"))
    eqcharset(upper, letter - lower)
    eqcharset(P.Set(""), P.Range(""))
    assert cs2str(P.Set("")) == ""
    eqcharset(P.Set("\0"), P.Match("\0"))
    eqcharset(P.Set("\1\0\2"), P.Range("\0\2"))
    eqcharset(P.Set("\1\0\2"), P.Range("\1\2") | P.Match("\0"))
    eqcharset(P.Set("\1\0\2") - P.Match("\0"), P.Range("\1\2"))


word = alpha ** 1 + (P.Any(1) - alpha) ** 0


def test_word():
    assert (word ** 0 + P.Any(-1))("alo alo")
    assert (word ** 1 + P.Any(-1))("alo alo")
    assert (word ** 2 + P.Any(-1))("alo alo")
    assert not (word ** 3 + P.Any(-1))("alo alo")
    assert not (word ** -1 + P.Any(-1))("alo alo")
    assert (word ** -2 + P.Any(-1))("alo alo")
    assert (word ** -3 + P.Any(-1))("alo alo")


eos = P.Any(-1)


def test_eos():
    assert (digit ** 0 + letter + digit + eos)("1298a1")
    assert not (digit ** 0 + letter + eos)("1257a1")


def test_matching_paren():
    b = P.Grammar(
        "(" + (((1 - P.Set("()")) | +P("(") + P.Var(0)) ** 0) + ")"
    )
    assert b("(a1())()")
    assert not (b + eos)("(a1())()")
    assert (b + eos)("((a1())()(é))")
    assert not b("(a1()()")


def test_word_exclusion():
    assert not (letter ** 1 - P.Match("for"))("foreach")
    assert (letter ** 1 - (P.Match("for") + eos))("foreach")
    assert not (letter ** 1 - (P.Match("for") + eos))("for")


# Now we need capture support...
def basiclookfor(p):
    return P.Grammar(p | 1+P.Var(0))


def caplookfor(p):
    return basiclookfor(P.Cap(p))


def test_initial_matches():
    b = P.Grammar(P.Match("(") +
              (((P.Any(1) - P.Set("()")) | +P.Match("(") + P.Var(0)) ** 0) +
              P.Match(")"))
    assert caplookfor(letter**1)("   4achou123...").captures == ["achou"]
    a = (caplookfor(letter**1)**0)(" two words, one more  ").captures
    assert a == ["two", "words", "one", "more"]

    assert basiclookfor((+b + 1) + P.CapP())("  (  (a)").pos == 6

    m = (P.Cap(digit**1 + P.CapC("d")) | P.Cap(letter**1 + P.CapC("l")))("123")
    assert m.captures == ["123", "d"]

    m = (P.Cap(digit**1) + "d" + (-1) | P.Cap(letter**1 + P.CapC("l")))("123d")
    assert m.captures == ["123"]

    m = (P.Cap(digit**1 + P.CapC("d")) | P.Cap(letter**1 + P.CapC("l")))("abcd")
    assert m.captures == ["abcd", "l"]


def test_capture_position():
    m = match(P.CapC(10, 20, 30) + 'a' + P.CapP(), 'aaa')
    assert m.captures == [10, 20, 30, 1]

    m = match(P.CapP() + P.CapC(10, 20, 30) + 'a' + P.CapP(), 'aaa')
    assert m.captures == [0, 10, 20, 30, 1]

    m = match(P.CapT(P.CapP() + P.CapC(10, 20, 30) + 'a' + P.CapP()), 'aaa')
    assert m.captures == [[0, 10, 20, 30, 1]]

    m = match(P.CapT(P.CapP() + P.CapC(7, 8) + P.CapC(10, 20, 30) + 'a' + P.CapP()), 'aaa')
    assert m.captures == [[0, 7, 8, 10, 20, 30, 1]]

    m = match(P.CapC() + P.CapC() + P.CapC(1) + P.CapC(2, 3, 4) + P.CapC() + 'a', 'aaa')
    assert m.captures == [1, 2, 3, 4]

    assert match(P.CapP() + letter**1 + P.CapP(), "abcd").captures == [0, 4]

    m = match(P.Grammar(P.Cap(P.Cap(1) + P.Var(0) | -1)), "abc")
    assert m.captures == ["abc", "a", "bc", "b", "c", "c", ""]


# test for small capture boundary
@pytest.mark.parametrize('i', xrange(250, 260))
def test_small_capture_boundary(i):
    assert match(P.Cap(i), 'a'*i).captures == ['a'*i]
    assert match(P.Cap(P.Cap(i)), 'a'*i).captures == ['a'*i, 'a'*i]


# tests for any*n
@pytest.mark.parametrize('n', xrange(1, 550))
def test_any(n):
    x_1 = 'x' * (n-1)
    x = x_1 + 'a'
    assert not match(P(n), x_1)
    assert match(P(n), x).pos == n
    assert n < 4 or match(P(n) | "xxx", x_1).pos == 3
    assert match(P.Cap(n), x).captures == [x]
    assert match(P.Cap(P.Cap(n)), x).captures == [x, x]
    assert match(P(-n), x_1).pos == 0
    assert not match(P(-n), x)
    assert n < 13 or match(P.CapC(20) + ((n - 13) + P(10)) + 3, x).captures == [20]
    n3 = n // 3
    assert match(n3 + P.CapP() + n3 + n3, x).captures == [n3]


def test_foo():
    assert match(P(0), "x").pos == 0
    assert match(P(0), "").pos == 0
    assert match(P.Cap(0), "x").captures == [""]
    assert match(P.CapC(0) + P(10) | P.CapC(1) + "xuxu", "xuxu").captures == [1]
    assert match(P.CapC(0) + P(10) | P.CapC(1) + "xuxu", "xuxuxuxuxu").captures == [0]
    assert match(P.Cap(P(2)**1), "abcde").captures == ["abcd"]
    p = P.CapC(0) + 1 | P.CapC(1) + 2 | P.CapC(2) + 3 | P.CapC(3) + 4


# test for alternation optimization
def test_alternation_optimization():
    assert match(P("a")**1 | "ab" | P("x")**0, "ab").pos == 1
    assert match((P("a")**1 | "ab" | P("x")**0 + 1)**0, "ab").pos == 2
    assert match(P("ab") | "cd" | "" | "cy" | "ak", "98").pos == 0
    assert match(P("ab") | "cd" | "ax" | "cy", "ax").pos == 2
    assert match("a" + P("b")**0 + "c"  | "cd" | "ax" | "cy", "ax").pos == 2
    assert match((P("ab") | "cd" | "ax" | "cy")**0, "ax").pos == 2
    assert match(P(1) + "x" | P.Set("") + "xu" | "ay", "ay").pos == 2
    assert match(P("abc") | "cde" | "aka", "aka").pos == 3
    assert match(P.Set("abc") + "x" | "cde" | "aka", "ax").pos == 2
    assert match(P.Set("abc") + "x" | "cde" | "aka", "aka").pos == 3
    assert match(P.Set("abc") + "x" | "cde" | "aka", "cde").pos == 3
    assert match(P.Set("abc") + "x" | "ide" | P.Set("ab") + "ka", "aka").pos == 3
    assert match("ab" | P.Set("abc") + P("y")**0 + "x" | "cde" | "aka", "ax").pos == 2
    assert match("ab" | P.Set("abc") + P("y")**0 + "x" | "cde" | "aka", "aka").pos == 3
    assert match("ab" | P.Set("abc") + P("y")**0 + "x" | "cde" | "aka", "cde").pos == 3
    assert match("ab" | P.Set("abc") + P("y")**0 + "x" | "ide" | P.Set("ab") + "ka", "aka").pos == 3
    assert match("ab" | P.Set("abc") + P("y")**0 + "x" | "ide" | P.Set("ab") + "ka", "ax").pos == 2
    assert match(P(1) + "x" | "cde" | P.Set("ab") + "ka", "aka").pos == 3
    assert match(P(1) + "x" | "cde" | P(1) + "ka", "aka").pos == 3
    assert match(P(1) + "x" | "cde" | P(1) + "ka", "cde").pos == 3
    assert match(P("eb") | "cd" | P("e")**0 | "x", "ee").pos == 2
    assert match(P("ab") | "cd" | P("e")**0 | "x", "abcd").pos == 2
    assert match(P("ab") | "cd" | P("e")**0 | "x", "eeex").pos == 3
    assert match(P("ab") | "cd" | P("e")**0 | "x", "cd").pos == 2
    assert match(P("ab") | "cd" | P("e")**0 | "x", "x").pos == 0
    assert match(P("ab") | "cd" | P("e")**0 | "x" | "", "zee").pos == 0
    assert match(P("ab") | "cd" | P("e")**1 | "x", "abcd").pos == 2
    assert match(P("ab") | "cd" | P("e")**1 | "x", "eeex").pos == 3
    assert match(P("ab") | "cd" | P("e")**1 | "x", "cd").pos == 2
    assert match(P("ab") | "cd" | P("e")**1 | "x", "x").pos == 1
    assert match(P("ab") | "cd" | P("e")**1 | "x" | "", "zee").pos == 0


def test_pi():
    pi = "3.14159 26535 89793 23846 26433 83279 50288 41971 69399 37510"

    p_stringcap = (P("1") / "a" | P("5") / "b" | P("9") / "c" | 1)**0
    p_querycap  = (P(1) / {"1": "a", "5": "b", "9": "c"})**0

    assert p_stringcap(pi).captures == p_querycap(pi).captures
    assert P.CapS(p_stringcap)(pi).captures == P.CapS(p_querycap)(pi).captures


#tests for capture optimizations
def test_capture_optimizations():
    assert match((P(3) | 4 + P.CapP()) + "a", "abca").pos == 4
    assert match(((P("a") | P.CapP()) + P("x"))**0, "axxaxx").captures == [2, 5]


#test for table captures
def test_table_captures():
    assert match(P.CapT(letter**1), "alo").captures == [[]]

    m = match(P.CapT(P.Cap(letter)**1) + P.CapC("t"), "alo")
    assert m.captures == [['a', 'l', 'o'], 't']

    m = match(P.CapT(P.Cap(P.Cap(letter)**1)), "alo")
    assert m.captures == [['alo', 'a', 'l', 'o']]

    # Same test case repeated
    #t = match(P.CapT(P.Cap(P.Cap(letter)**1)), "alo")
    #assert(table.concat(t, ";") == "alo;a;l;o")

    m = match(P.CapT(P.CapT((P.CapP() + letter + P.CapP())**1)), "alo")
    assert m.captures == [[[0, 1, 1, 2, 2, 3]]]

    m = match(P.CapT(P.Cap(P.Cap(1) + 1 + P.Cap(1))), "alo")
    assert m.captures == [["alo", "a", "o"]]


# tests for groups
def test_groups():
    p = P.CapG(1)   # no capture
    assert(match(p, 'x').captures == ['x'])
    #p = P.CapG(P(True)/function () end * 1)   -- no value
    #assert(p:match('x') == 'x')
    p = P.CapG(P.CapG(P.CapG(P.Cap(1))))
    assert(match(p, 'x').captures == ['x'])
    p = P.CapG(P.CapG(P.CapG(P.Cap(1))**0) + P.CapG(P.CapC(1) + P.CapC(2)))
    assert match(p, 'abc').captures == ['a', 'b', 'c', 1, 2]


# test for non-pattern as arguments to pattern functions
def test_non_pattern_as_arguments():
    p = (  P.Grammar(('a' + P.Var(0))**-1)
         + P('b')
         + P.Grammar('a' + P.Var(1), P.Var(0)**-1)
    )
    assert match(p, "aaabaac").pos == 6


# a large table capture
def test_large_table_capture():
    t = match(P.CapT(P.Cap('a')**0), 'a'*10000)
    assert t.captures == [['a']*10000]


# test for errors
def test_for_errors():
    with pytest.raises(RuntimeError) as excinfo:
        P.Grammar(P.Var(0) + 'a')
    assert excinfo.value.message == 'Rule 0 is left recursive'

    # TODO Either remove this test or confirm there are no overflows when
    #      there are many captures.

    # The original Lua test fails because the Lua virtual stack can't grow
    # beyond approx 32768 (2**15) entries. The full Lua error message is
    # "stack overflow (too many captures)".

    # CPython doesn't have this limitation, so we're probably only
    # theoretically limited by how much we can malloc.
    # However there are various lpeg.c structures that store e.g. capture
    # stack indexes. Those are a mixture of short and int, which could
    # overflow now we've shed the shackles of Lua's virtual stack.

    #with pytest.raises(RuntimeError) as excinfo:
    #    match(P.Cap('a')**0, "a" * 50000)
    #assert excinfo.value.message == "Capture stack overflow"

    with pytest.raises(RuntimeError) as excinfo:
        match(P.Var(0), '')
    assert excinfo.value.message == "Reference to rule outside a grammar"

    with pytest.raises(RuntimeError) as excinfo:
        match(P.Var('hiii'), '')
    assert excinfo.value.message == "Reference to rule outside a grammar"

    with pytest.raises(RuntimeError) as excinfo:
        match(P.Grammar(P.Var('hiii')), '')
    # TODO The exception matches, but wrong error message: invalid op code
    #assert excinfo.value.message == "rule 'hiii' is not defined"

    with pytest.raises(RuntimeError) as excinfo:
        match(P.Grammar(P.Var({})), '')
    # TODO The exception matches, but wrong error message: invalid op code
    #assert excinfo.value.message == "rule <a table> is not defined"


def test_grammar():
    Var = P.Var

    Space     = P.Set(' \n\t')**0
    Number    = P.Cap(P.Range('09')**1) + Space
    FactorOp  = P.Cap(P.Set("+-")) + Space
    TermOp    = P.Cap(P.Set("*/")) + Space
    Open      = "(" + Space
    Close     = ")" + Space

    def f_factor(v1, op, v2, d):
        assert d is None
        if op == '+':
            return v1 + v2
        else:
            return v1 - v2

    def f_term(v1, op, v2, d):
        assert d is None
        if op == '*':
            return v1 * v2
        else:
            return v1 / v2

    G = P.Grammar(
        start="Exp",
        Exp     = P.CapF(Var("Factor") + P.CapG(FactorOp + Var("Factor"))**0, f_factor),
        Factor  = P.CapF(Var("Term") + P.CapG(TermOp + Var("Term"))**0, f_term),
        Term    = Number / int | Open + Var("Exp") + Close,
    )
    G = Space + G + -1

    #assert match(G, " 3 + 5*9 / (1+1) "     ).captures == [25]
    #assert match(G, "3+4/2"                 ).captures == [5]
    #assert match(G, "3+3-3- 9*2+3*9/1-  8"  ).captures == [4]


# test for grammars (errors deep in calling non-terminals)
def test_grammar2():
    g = P.Grammar(
      P.Var(1) | "a",
      "a" + P.Var(2) + "x",
      "b" + P.Var(2) | "c",
    )
    assert match(g, "abbbcx").pos == 6
    assert match(g, "abbbbx").pos == 1


# tests for \0
def test_nul_char():
    assert match(P.Range("\x00\x01")**1, "\x00\x01\x000").pos == 3
    assert match(P.Set("\x00\x01ab")**1, "\x00\x01\x00a").pos == 4
    assert match(P(1)**3, "\x00\x01\x00a").pos == 4
    assert not match(P(-4), "\x00\x01\x00a")
    assert match(P("\x00\x01\x00a"), "\x00\x01\x00a").pos == 4
    assert match(P("\x00\x00\x00"), "\x00\x00\x00").pos == 3
    assert not match(P("\x00\x00\x00"), "\x00\x00")


# tests for predicates
def test_predicates():
    assert not match(-P("a") + 2, "alo")
    assert match(- -P("a") + 2, "alo").pos == 2
    assert match(+P("a") + 2, "alo").pos == 2
    assert match(++P("a") + 2, "alo").pos == 2
    assert not match(++P("c") + 2, "alo")
    assert match(P.CapS((++P("a") + 1 | P(1)/".")**0), "aloal").captures == ["a..a."]
    assert match(P.CapS((+((+P("a"))/"") + 1 | P(1)/".")**0), "aloal").captures == ["a..a."]
    assert match(P.CapS((- -P("a") + 1 | P(1)/".")**0), "aloal").captures == ["a..a."]
    assert match(P.CapS((-((-P("a"))/"") + 1 | P(1)/".")**0), "aloal").captures == ["a..a."]


# tests for Tail Calls

# create a grammar for a simple DFA for even number of 0s and 1s
# finished in '$':
#
#  ->0 <---0---> 1
#    ^           ^
#    |           |
#    1           1
#    |           |
#    V           V
#    2 <---0---> 3
#
# this grammar should keep no backtracking information


def test_tail_calls():
    p = P.Grammar(
        '0' + P.Var(1) | '1' + P.Var(2) | '$',
        '0' + P.Var(0) | '1' + P.Var(3),
        '0' + P.Var(3) | '1' + P.Var(0),
        '0' + P.Var(2) | '1' + P.Var(1),
    )
    assert match(p, "00" * 10000 + "$")
    assert match(p, "01" * 10000 + "$")
    assert match(p, "011" * 10000 + "$")
    assert not match(p, "011" * 10001 + "$")


# tests for optional start position
def test_start_position():
    #assert match("a", "abc", 1))
    #assert match("b", "abc", 2))
    #assert match("c", "abc", 3))
    #assert(not m.match(1, "abc", 4))
    #assert match("a", "abc", -3))
    #assert match("b", "abc", -2))
    #assert match("c", "abc", -1))
    #assert match("abc", "abc", -4))   -- truncate to position 1

    #assert match("", "abc", 10))   -- empty string is everywhere!
    #assert match("", "", 10))
    #assert(not m.match(1, "", 1))
    #assert(not m.match(1, "", -1))
    #assert(not m.match(1, "", 0))
    pass


# basic tests for external C function

def test_external_function():
    #assert match(m.span("abcd"), "abbbacebb") == 7)
    #assert match(m.span("abcd"), "0abbbacebb") == 1)
    #assert match(m.span("abcd"), "") == 1)
    pass


# tests for argument captures
def test_argument_captures():
    with pytest.raises(ValueError) as excinfo: P.CapA(0)
    assert excinfo.value.message == "Argument ID out of range"

    with pytest.raises(ValueError) as excinfo: P.CapA(-1)
    assert excinfo.value.message == "Argument ID out of range"

    with pytest.raises(ValueError) as excinfo: P.CapA(2**18)
    assert excinfo.value.message == "Argument ID out of range"

    with pytest.raises(IndexError) as excinfo: P.CapA(1)('a')
    assert excinfo.value.message == "tuple index out of range"
    del excinfo

    assert P.CapA(1)('a', print).captures == [print]
    assert (P.CapA(1) + P.CapA(2))('', 10, 20).captures == [10, 20]

    def f1(s,i,x):
        assert s == 'a'
        assert i == 1
        return i, x+1

    def f2(s,i,a,b,c):
        assert s == 'a'
        assert i == 1
        assert c == None
        return i, 2*a + 3*b

    #assert (P.CapRT(P.CapG(P.CapA(3), "a") +
    #                 P.CapRT(P.CapB("a"), f1) +
    #                 P.CapA(2), f2) + "a")("a", False, 100, 1000).captures == [2*1001 + 3*100]


# tests for Lua functions
def test_functions():
    t = {}
    s = ""
    def p(s1, i):
        assert s == s1
        t.append(i)

    s = "hi, this is a test"
    #assert match( ((p - P(-1)) | 2)**0, s).pos == len(s)
    #assert len(t) == len(s)/2 and t[0] == 1 and t[1] == 3

    #assert not match(p, s)

    #p = mt.__add(function (s, i) return i end, function (s, i) return nil end)
    #assert match(p, "alo"))

    #p = mt.__mul(function (s, i) return i end, function (s, i) return nil end)
    #assert(not m.match(p, "alo"))


    #t = {}
    #p = function (s1, i) assert(s == s1); t[#t + 1] = i; return i end
    #s = "hi, this is a test"
    #assert match((m.P(1) * p)**0, s) == string.len(s) + 1)
    #assert(#t == string.len(s) and t[1] == 2 and t[2] == 3)

    #t = {}
    #p = m.P(function (s1, i) assert(s == s1); t[#t + 1] = i;
    #                         return i <= s1:len() and i + 1 end)
    #s = "hi, this is a test"
    #assert match(p**0, s) == string.len(s) + 1)
    #assert(#t == string.len(s) + 1 and t[1] == 1 and t[2] == 2)

    #p = function (s1, i) return m.match(m.P"a"^1, s1, i) end
    #assert match(p, "aaaa") == 5)
    #assert match(p, "abaa") == 2)
    #assert(not m.match(p, "baaa"))

    #assert(not pcall(m.match, function () return 2^20 end, s))
    #assert(not pcall(m.match, function () return 0 end, s))
    #assert(not pcall(m.match, function (s, i) return i - 1 end, s))
    #assert(not pcall(m.match, m.P(1)**0 * function (_, i) return i - 1 end, s))
    #assert match(m.P(1)**0 * function (_, i) return i end * -1, s))
    #assert(not pcall(m.match, m.P(1)**0 * function (_, i) return i + 1 end, s))
    #assert match(m.P(function (s, i) return s:len() + 1 end) * -1, s))
    #assert(not pcall(m.match, m.P(function (s, i) return s:len() + 2 end) * -1, s))
    #assert(not m.match(m.P(function (s, i) return s:len() end) * -1, s))
    #assert match(m.P(1)**0 * function (_, i) return true end, s) ==
    #       string.len(s) + 1)
    #for i = 1, string.len(s) + 1 do
    #  assert match(function (_, _) return i end, s) == i)
    #end

    #p = (m.P(function (s, i) return i%2 == 0 and i + 1 end)
    #  +  m.P(function (s, i) return i%2 ~= 0 and i + 2 <= s:len() and i + 3 end))**0
    #  * -1
    #assert(p:match(string.rep('a', 14000)))


# tests for Function Replacements
def test_function_replacements():
    def f(a, *args):
        if a != "x":
            return [a] + list(args)

    assert match(P.Cap(1)**0, "abc").captures == ["a", "b", "c"]
    assert match(P.Cap(1)**0 / f, "abc").captures == [["a", "b", "c"]]
    assert match(P.Cap(1)**0 / f / f, "abc").captures == [[["a", "b", "c"]]]

    assert match(P(1)**0, "abc").captures == [] #   -- no capture
    assert match(P(1)**0 / f, "abc").captures == [['abc']] #   -- no capture
    assert match(P(1)**0 / f / f, "abc").captures == [[['abc']]] #   -- no capture

    assert match( P(1)**0   + P.CapP(), "abc").captures == [3]
    assert match((P(1)**0/f + P.CapP())/f, "abc").captures == [[['abc'], 3]]
    assert match((P.Cap(1)**0/f + P.CapP())/f, "abc").captures == [[["a", "b", "c"], 3]]

    assert match((P.Cap(1)**0/f + P.CapP())/f, "xbc").captures == [[None, 3]]

    assert match(P.Cap(P.Cap(1)**0)/f, "abc").captures == [["abc", "a", "b", "c"]]

    def g(*args):
        return [1]+list(args)

    #assert match(P.Cap(P(1))**0/g/g, "abc").captures == {1, 1, "a", "b", "c"}

    #assert match(( P.CapC([None,None,4]) +
    #               P.CapC([None,3]) +
    #               P.CapC([None, None]) ) / g / g,
    #             "").captures == [1, 1, None, None, 4, None, 3, None, None]

    def f(x):
        return x, x + 'x'
    #assert match((P.Cap(P(1)) / f)**0, "abc").captures == ["a", "ax", "b", "bx", "c", "cx"]

    def swap(x, y):
        return y, x

    #assert match(P.CapT((P.Cap(P(1)) / swap + P.CapC(1))**0), "abc").captures == [None, "a", 1, None, "b", 1, None, "c", 1]


# tests for Query Replacements
def test_query_replacements():
    assert match(P.Cap(P.Cap(1)**0) / {'abc': 10}, "abc").captures == [10]
    assert match(P.Cap(1)**0 / {'a': 10}, "abc").captures == [10]
    assert match(P.Set("ba")**0 / {"ab": 40}, "abc").captures == [40]
    #assert match(P.CapT((P.Set("ba")/{'a': 40})**0), "abc").captures == [[40]]

    assert match(P.CapS((P.Cap(1)/{'a':'.', 'd':'..'})**0), "abcdde").captures == [".bc....e"]
    assert match(P.CapS((P.Cap(1)/{'f':"."})**0), "abcdde").captures == ["abcdde"]
    assert match(P.CapS((P.Cap(1)/{'d':"."})**0), "abcdde").captures == ["abc..e"]
    assert match(P.CapS((P.Cap(1)/{'e':"."})**0), "abcdde").captures == ["abcdd."]
    assert match(P.CapS((P.Cap(1)/{'e':".", 'f':"+"})**0), "eefef").captures == ["..+.+"]
    assert match(P.CapS((P.Cap(1))**0), "abcdde").captures == ["abcdde"]
    assert match(P.CapS(P.Cap(P.Cap(1)**0)), "abcdde").captures == ["abcdde"]
    assert match(1 + P.CapS(P(1)**0), "abcdde").captures == ["bcdde"]
    assert match(P.CapS((P.Cap('0')/'x' | 1)**0), "abcdde").captures == ["abcdde"]
    assert match(P.CapS((P.Cap('0')/'x' | 1)**0), "0ab0b0").captures == ["xabxbx"]
    assert match(P.CapS((P.Cap('0')/'x' | P(1)/{'b':3})**0), "b0a0b").captures == ["3xax3"]
    assert match(P(1)/'%0%0'/{'aa': -3} + 'x', 'ax').captures == [-3]
    assert match(P.Cap(1)/'%0%1'/{'aa': 'z'}/{'z': -3} + 'x', 'ax').captures == [-3]

    assert match(P.CapS(P.CapC(0) + (P(1)/"")), "4321").captures == ["0"]

    assert match(P.CapS((P(1) / "%0")**0), "abcd").captures == ["abcd"]
    assert match(P.CapS((P(1) / "%0.%0")**0), "abcd").captures == ["a.ab.bc.cd.d"]
    assert match(P.CapS((P("a") / "%0.%0" | 1)**0), "abcad").captures == ["a.abca.ad"]
    assert match(P.Cap("a") / "%1%%%0", "a").captures == ["a%a"]
    assert match(P.CapS((P(1) / ".xx")**0), "abcd").captures == [".xx.xx.xx.xx"]
    #assert match(P.CapP() + P(3) + P.CapP()/"%2%1%1 - %0 ",
    #             "abcde").captures == ["411 - abc "]

    #assert(pcall(m.match, m.P(1)/"%0", "abc"))
    #assert(not pcall(m.match, m.P(1)/"%1", "abc"))   -- out of range
    #assert(not pcall(m.match, m.P(1)/"%9", "abc"))   -- out of range

    p = P.Cap(1)
    p = p + p; p = p + p; p = p + p + P.Cap(1) / "%9 - %1"
    #assert match(p, "1234567890").captures == ["9 - 1"]

    assert match(P.CapC(print), "").captures == [print]


# too many captures (just ignore extra ones)
def test_ignore_extra_captures():
    s = "01234567890123456789"
    assert match(P.Cap(1)**0 / "%2-%9-%0-%9", s).captures == ["1-8-01234567890123456789-8"]
    s = "12345678901234567890" * 20
    assert match(P.Cap(1)**0 / "%9-%1-%0-%3", s).captures == ["9-1-" + s + "-3"]


# string captures with non-string subcaptures
def test_string_captures_with_non_string_subcaptures():
    p = (P.CapC('alo') + P.Cap(1)) / "%1 - %2 - %1"
    assert match(p, 'x').captures == ['alo - x - alo']


def test_ekwfniasd():
    assert match(P.CapC(True) / "%1", "a").captures == ['True'] # TODO: Huh?


# long strings for string capture
def test_long_strings_for_string_capture():
    n = 10000
    s = 'a'*n + 'b'*n + 'c'*n
    p = (P.Cap(P('a')**1) + P.Cap(P('b')**1) + P.Cap(P('c')**1)) / '%3%2%1'
    assert match(p, s).captures == ['c'*n + 'b'*n + 'a'*n]


# accumulator capture
def test_accumulator_capture():
    def f(x, *args):
        return x + 1

    assert match(P.CapF(P.CapC(0) + P.Cap(1)**0, f), "alo alo").captures == [7]

    def head(seq):
        return seq[0]

    #assert match(P.CapF(P.CapC([1,2,3]), head), "").captures == [[1]]

    p = P.CapF(P.CapT(P(True)) + P.CapG(P.Cap(P.Range("az")**1)+ "=" +
                                         P.Cap(P.Range("az")**1) + ";")**0,
               dict)
    #assert match(p, "a=b;c=du;xux=yuy;") #.captures == {'a': "b", 'c': "du", 'xux': "yuy"}


# tests for loop checker

def haveloop(p):
    with pytest.raises(ValueError) as excinfo:
        (p)**0
    assert excinfo.value.message == "Loop body may accept empty string"

def test_haveloop():
    haveloop(P("x")**-4)
    assert match(((P(0) | 1) + P.Set("al"))**0, "alo").pos == 2
    assert match((("x" | +P(1))**-4 + P.Set("al"))**0, "alo").pos == 2
    haveloop(P(""))
    haveloop(P("x")**0)
    haveloop(P("x")**-1)
    haveloop(P("x") | 1 | 2 | P("a")**-1)
    haveloop(-P("ab"))
    haveloop(- -P("ab"))
    haveloop(+ +(P("ab") | "xy"))
    haveloop(- +P("ab")**0)
    haveloop(+ -P("ab")**1)
    haveloop(+P.Var(3))
    haveloop(P.Var(3) | P.Var(1) | P('a')**-1)
    haveloop(P.Grammar(P.Var(1) + P.Var(2), P.Var(2), P(0)))
    assert match(P.Grammar(P.Var(1) + P.Var(2), P.Var(2), P(1))**0, "abc").pos == 2
    assert match(P("")**-3, "a").pos == 0


def test_badgrammar():
    with pytest.raises(RuntimeError): P.Grammar(P.Var(0)) # Rule 0 is left recursive
    #with pytest.raises(RuntimeError): P.Grammar(P.Var(1)) #, "rule '2'")   # invalid non-terminal
    #with pytest.raises(RuntimeError): P.Grammar(P.Var("x")) #, "rule 'x'")   # invalid non-terminal
    #with pytest.raises(RuntimeError): P.Grammar(P.Var({})) #, "rule <a table>")   -- invalid non-terminal
    with pytest.raises(RuntimeError): P.Grammar(+P("a") + P.Var(0)) #, "rule '1'")
    with pytest.raises(RuntimeError): P.Grammar(-P("a") + P.Var(0)) #, "rule '1'")
    with pytest.raises(RuntimeError): P.Grammar( -1 + P.Var(0)) #, "rule '1'")
    with pytest.raises(RuntimeError): P.Grammar(1 + P.Var(1), P.Var(1)) #}, "rule '2'")
    with pytest.raises(RuntimeError): P.Grammar(P(0), 1 + P.Var(0)**0) #, "loop in rule '2'")
    with pytest.raises(RuntimeError): P.Grammar(P.Var(1), P.Var(2)**0, P("")) # }, "rule '2'")
    with pytest.raises(RuntimeError): P.Grammar(P.Var(1) + P.Var(2)**0, P.Var(2)**0, P("")) #, "rule '1'")
    #with pytest.raises(RuntimeError): P.Grammar(+(P.Var(1) + 'a') )#, "rule '1'")
    #with pytest.raises(RuntimeError): P.Grammar(-(P.Var(1) + 'a') )#, "rule '1'")
    assert match(P.Grammar('a' + -P.Var(0)), "aaa").pos == 1
    assert match(P.Grammar('a' + -P.Var(0)), "aaaa").pos == -1


# simple tests for maximum sizes:
def test_maximum_sizes():
    p = P("a")
    for i in range(14):
        p = p + p

    p = [P('a') for i in range(100)]
    p = P.Grammar(*p)

# initial rule
def test_initial_rule():
    g = []
    #for i = 1, 10 do g["i%i"..i] =  "a" * m.V("i"..i+1) end
    #g.i11 = m.P""
    #for i = 1, 10 do
    #  g[1] = "i"..i
    #  local p = m.P(g)
    #  assert(p:match("aaaaaaaaaaa") == 11 - i + 1)
    #end

# tests for back references
def test_back_references():
    with pytest.raises(RuntimeError): match(P.CapB('x'), '')
    with pytest.raises(RuntimeError): match(P.CapG(1, 'a') + P.CapB('b'), 'a')

    p = P.CapG(P.Cap(1) + P.Cap(1), "k") + P.CapT(P.CapB("k"))
    #assert match(p, "ab").captures == ["a", "b"] # Infinite loop

    t = []
    def foo(p):
        t.append(p)
        return p + "x"

    p = (P.CapG(P.Cap(2)    / foo, "x") + P.CapB("x") +
         P.CapG(P.CapB('x') / foo, "x") + P.CapB("x") +
         P.CapG(P.CapB('x') / foo, "x") + P.CapB("x") +
         P.CapG(P.CapB('x') / foo, "x") + P.CapB("x")
    )
    assert match(p, 'ab').captures == ['abx', 'abxx', 'abxxx', 'abxxxx']
    assert t == ['ab', 'ab', 'abx',
                  'ab', 'abx', 'abxx',
                  'ab', 'abx', 'abxx', 'abxxx']


# tests for match-time captures
def test_match_time_captures():
    def chr_(subject):
        return chr(int(subject))

    def id_(subject, pos, captures):
        return True, captures

    assert match(P.CapRT(P.CapS((P.CapRT(P.Set('abc') / {'a': 'x', 'c': 'y'}, id_) |
                                 P.Range('09')**1 / chr_ |
                                 P(1))**0), id_),
                 "acb98+68c").captures == ["xybb+Dy"]

    p = P.Grammar(
        start='S',
        S = P.Var('atom') + space
          | P.CapRT(P.CapT("(" + space + (P.CapRT(P.Var('S')**1, id_) | P(0)) + ")" + space), id_),
        atom = P.CapRT(P.Cap(P.Range("AZaz09")**1), id_),
    )
    m = match(p, "(a g () ((b) c) (d (e)))")
    assert m.captures == [ ['a', 'g', [], [['b'], 'c'], ['d', ['e']]] ]

    s = 'a' * 500
    assert match(P.CapRT(P(1), id_)**0, s).captures == ['a'] * 500
    #with  match(P.CapRT(1, id_)**0, 'a' * 50000))

    def id_(s, i, x):
        if x == 'a':
            return i + 1, 1, 3, 7
        else:
            return None, 2, 4, 6, 8

    # TypeError: Pattern argument must be None,
    #            or convertible to a string or an integer
    #p = (P(id_) | P.CapRT(1, id_) | P.CapRT(0, id_))**0
    #assert match(p, 'abababab') == ['137' * 4]

    def ref(s, i, x):
        return match(x, s, i - len(x))

    #assert(m.Cmt(m.P(1)^0, ref):match('alo') == 4)
    #assert((m.P(1) * m.Cmt(m.P(1)^0, ref)):match('alo') == 4)
    #assert(not (m.P(1) * m.Cmt(m.C(1)^0, ref)):match('alo'))

    def ref(s, i, x):
        return i == int(x[0]) and i, ['xuxu']

    assert match(P.CapRT(1, ref), '1').captures == ['xuxu']
    assert not match(P.CapRT(1, ref), '0')
    assert match(P.CapRT(P(1)**0, ref), '02').captures == ['xuxu']

    def ref(subject, position, (a, b)):
        if a == b:
            return position, [a.upper()]

    p = P.CapRT(P.Cap(P.Range("az")**1) + "-" + P.Cap(P.Range("az")**1), ref)
    p = (any_ - p)**0 + p + any_**0 + -1

    assert match(p, 'abbbc-bc ddaa').captures == ['BC']

    def f(subject, position, (s1, s2)):
        return s1 == s2

    def g(*args):
        pass

    c = ('[' + P.CapG(P('=')**0, "init") + '[' +
         P.Grammar(  P.CapRT(']' + P.Cap(P('=')**0) + ']' + P.CapB("init"), f)
                    | 1 + P.Var(0)) / g)

    assert match(c, '[==[]]====]]]]==]===[]').pos == 17
    assert match(c, '[[]=]====]=]]]==]===[]').pos == 13
    assert not match(c, '[[]=]====]=]=]==]===[]')
