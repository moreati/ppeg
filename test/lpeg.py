# -*- coding: latin1 -*-
"""A port of the lpeg test suite to ppeg.

This is a direct port of as much of the lpeg test suite as makes sense for
the ppeg port of lpeg.
"""

# Shorthand for the pattern type
from _ppeg import Pattern as P
import sys

# General tests for the ppeg library
def test_type():
    assert type("alo") != P
    assert type(sys.stdin) != P
    assert type(P.Match("alo")) == P

# Tests for some basic optimizations
def test_basic_opt_choice():
    p = P.Fail() | P.Match("a")
    assert p("a") == 1
    p = P() | P.Match("a")
    assert p("a") == 0
    p = P.Match("a") | P.Fail()
    assert p("b") == None
    p = P.Match("a") | P()
    assert p("b") == 0

def test_basic_opt_concat():
    p = P.Fail() + P.Match("a")
    assert p("a") == None
    p = P() + P.Match("a")
    assert p("a") == 1
    p = P.Match("a") + P.Fail()
    assert p("a") == None
    p = P.Match("a") + P()
    assert p("a") == 1

def test_basic_opt_assert():
    p = +P.Fail() + P.Match("a")
    assert p("a") == None
    p = +P() + P.Match("a")
    assert p("a") == 1
    p = P.Match("a") + +P.Fail()
    assert p("a") == None
    p = P.Match("a") + +P()
    assert p("a") == 1

# Locale tests not included yet

# Ugly "is (not) None", because 0 is a valid match result, but is false
# Consider changing the API...
def test_match_any():
    assert P.Any(3)("aaaa") is not None
    assert P.Any(4)("aaaa") is not None
    assert P.Any(5)("aaaa") is None
    assert P.Any(-3)("aa") is not None
    assert P.Any(-3)("aaa") is None
    assert P.Any(-3)("aaaa") is None
    assert P.Any(-4)("aaaa") is None
    assert P.Any(-5)("aaaa") is not None

def test_match_match():
    assert P.Match("a")("alo") == 1
    assert P.Match("al")("alo") == 2
    assert P.Match("alu")("alo") is None
    assert P()("") == 0

# Character sets
def cs2str(c):
    return "".join(chr(ch) for ch in range(256) if c(chr(ch)) is not None)
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
    assert (word ** 0 + P.Any(-1))("alo alo") is not None
    assert (word ** 1 + P.Any(-1))("alo alo") is not None
    assert (word ** 2 + P.Any(-1))("alo alo") is not None
    assert (word ** 3 + P.Any(-1))("alo alo") is None
    assert (word ** -1 + P.Any(-1))("alo alo") is None
    assert (word ** -2 + P.Any(-1))("alo alo") is not None
    assert (word ** -3 + P.Any(-1))("alo alo") is not None

eos = P.Any(-1)

def test_eos():
    assert (digit ** 0 + letter + digit + eos)("1298a1") is not None
    assert (digit ** 0 + letter + eos)("1257a1") is None

b = P.Grammar(P.Match("(") +
              (((P.Any(1) - P.Set("()")) | +P.Match("(") + P.Var(0)) ** 0) +
              P.Match(")"))

def test_matching_paren():
    assert b("(a1())()") is not None
    assert (b + eos)("(a1())()") is None
    assert (b + eos)("((a1())()(é))") is not None
    assert b("(a1()()") is None

def test_word_exclusion():
    assert (letter ** 1 - P.Match("for"))("foreach") is None
    assert (letter ** 1 - (P.Match("for") + eos))("foreach") is not None
    assert (letter ** 1 - (P.Match("for") + eos))("for") is None

# Now we need capture support...
