# -*- coding: latin1 -*-
"""A port of the lpeg test suite to ppeg.

This is a direct port of as much of the lpeg test suite as makes sense for
the ppeg port of lpeg.
"""

import sys

from _ppeg import Pattern as P


# General tests for the ppeg library
def test_type():
    assert type("alo") != P
    assert type(sys.stdin) != P
    assert type(P.Match("alo")) == P


# Tests for some basic optimizations
def test_basic_opt_choice():
    p = P.Fail() | P.Match("a")
    assert p("a").pos == 1
    p = P() | P.Match("a")
    assert p("a").pos == 0
    p = P.Match("a") | P.Fail()
    assert not p("b")
    p = P.Match("a") | P()
    assert p("b").pos == 0


def test_basic_opt_concat():
    p = P.Fail() + P.Match("a")
    assert not p("a")
    p = P() + P.Match("a")
    assert p("a").pos == 1
    p = P.Match("a") + P.Fail()
    assert not p("a")
    p = P.Match("a") + P()
    assert p("a").pos == 1


def test_basic_opt_assert():
    p = +P.Fail() + P.Match("a")
    assert not p("a")
    p = +P() + P.Match("a")
    assert p("a").pos == 1
    p = P.Match("a") + +P.Fail()
    assert not p("a")
    p = P.Match("a") + +P()
    assert p("a").pos == 1


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


b = P.Grammar(P.Match("(") +
              (((P.Any(1) - P.Set("()")) | +P.Match("(") + P.Var(0)) ** 0) +
              P.Match(")"))


def test_matching_paren():
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
