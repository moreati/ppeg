from __future__ import with_statement
import unittest
from _ppeg import Pattern
import sys
from cStringIO import StringIO
from contextlib import contextmanager

@contextmanager
def stdout(fd):
    old = sys.stdout
    sys.stdout = fd
    yield fd
    sys.stdout = old

class TestBasic(unittest.TestCase):
    def testpat(self):
        p = Pattern()
        self.assertEqual(type(p), Pattern)
    def testdisplay(self):
        p = Pattern.Any(1)
        with stdout(StringIO()) as s:
            p.display()
        self.assertNotEqual(s.getvalue(), '')

class TestBuild(unittest.TestCase):
    def match(self, pat, items):
        self.assertEqual([i[0] for i in pat.dump()], items + ['end'])
    def testany(self):
        self.match(Pattern.Any(0), [])
        self.match(Pattern.Any(1), ['any'])
        # Negative self.match - any & fail
        self.match(Pattern.Any(-1), ['any', 'fail'])
        # Overflow - max of 255 per opcode
        self.match(Pattern.Any(300), ['any', 'any'])
    def testmatch1(self):
        self.match(Pattern.Match('a'), ['char'])
    def testmatch2(self):
        self.match(Pattern.Match('ab'), ['char', 'char'])
    def testfail(self):
        self.match(Pattern.Fail(), ['fail'])
    def testset(self):
        p = Pattern.Set("abdef")
        self.match(p, ['set'])
        self.assertEqual([p(s) for s in "abcdefgh"],
                [1, 1, None, 1, 1, 1, None, None])
    def testrange(self):
        p = Pattern.Range("bceg")
        self.match(p, ['set'])
        self.assertEqual([p(s) for s in "abcdefgh"],
                [None, 1, 1, None, 1, 1, 1, None])

class TestConcat(unittest.TestCase):
    def testany(self):
        p1 = Pattern.Any(2)
        p2 = Pattern.Any(1) + Pattern.Any(1)
        self.assertEqual(p1.dump(), p2.dump())
    def testid(self):
        p1 = Pattern.Dummy()
        p2 = Pattern() + p1
        self.assertEqual(p1.dump(), p2.dump())
        p2 = p1 + Pattern()
        self.assertEqual(p1.dump(), p2.dump())
    def testfail(self):
        p1 = Pattern.Dummy()
        p2 = Pattern.Fail() + p1
        self.assertEqual(p2.dump(), Pattern.Fail().dump())
        p2 = p1 + Pattern.Fail()
        self.assertEqual(p2.dump(), Pattern.Fail().dump())
    def testmatch(self):
        splits = [
            ('a', 'bcd'),
            ('ab', 'cd'),
            ('abc', 'd'),
            ('a', '', 'bcd'),
            ('a', 'b', 'cd'),
            ('a', 'bc', 'd'),
            ('ab', '', 'cd'),
            ('ab', 'c', 'd'),
            ('abc', '', 'd'),
            ('a', 'b', 'c', 'd'),
        ]
        p1 = Pattern.Match("abcd")
        for split in splits:
            p2 = Pattern.Match(split[0])
            for s in split[1:]:
                p2 = p2 + Pattern.Match(s)
            self.assertEqual(p1.dump(), p2.dump())
            p2 = Pattern.Match('')
            for s in split:
                p2 = p2 + Pattern.Match(s)
            self.assertEqual(p1.dump(), p2.dump())
            p2 = Pattern.Match(split[0])
            for s in split[1:]:
                p2 = p2 + Pattern.Match(s)
            p2 = p2 + Pattern.Match('')
            self.assertEqual(p1.dump(), p2.dump())

class TestAnd(unittest.TestCase):
    def match(self, pat, items):
        self.assertEqual([i[0] for i in pat.dump()], items + ['end'])
    def testtrue(self):
        self.match(+Pattern(), [])
    def testfail(self):
        self.match(+Pattern.Fail(), ['fail'])
    def testanycset(self):
        # &Any(1) is a 0-length match of any character. This is
        # optimised as a match against a character set with every bit set -
        # if the match succeeds, the pattern fails.
        self.match(+Pattern.Any(1), ['set', 'fail'])
    def testother(self):
        self.match(+Pattern.Any(5), ['choice', 'any', 'back_commit', 'fail'])
    def testlonger(self):
        p = Pattern.Any(1) + Pattern.Match('ab')
        self.match(+p, ['choice', 'any', 'char', 'char', 'back_commit', 'fail'])

class TestPow(unittest.TestCase):
    # TODO: Add more tests!!!
    def match(self, pat, items):
        self.assertEqual([i[0] for i in pat.dump()], items + ['end'])
    def testsimple(self):
        self.match(Pattern.Any(1) ** 3, ['any', 'any', 'any', 'span'])
    def testmatch(self):
        p = Pattern.Match("foo") ** 0
        self.assertEqual(p("boofoo"), 0)
        self.assertEqual(p("foofoo"), 6)

class TestDiff(unittest.TestCase):
    def match(self, pat, items):
        self.assertEqual([i[0] for i in pat.dump()], items + ['end'])
    def testtrue(self):
        self.match(-Pattern(), ['fail'])
    def testfail(self):
        self.match(-Pattern.Fail(), [])
    def testdiff(self):
        # Not an obvious translation - the optimizer hits us. This has been
        # validated against the Lua lpeg implementation
        p = Pattern.Match('bc') - Pattern.Match('ef')
        self.match(p, ['char', 'choice', 'char', 'failtwice', 'char', 'char'])

class TestCapture(unittest.TestCase):
    def captype(self, n, p):
        return p.dump()[n][1] & 0xF
    def match(self, pat, items):
        self.assertEqual([i[0] for i in pat.dump()], items + ['end'])
    def testfullcap(self):
        p = Pattern.Cap(Pattern.Any(1))
        self.match(p, ['any', 'fullcapture'])
        off = p.dump()[1][2]
        # Csimple == 5
        self.assertEqual(self.captype(1, p), 5)
        self.assertEqual(off, 0)
    def testopenclose(self):
        p = Pattern.Cap(Pattern.Any(1)**1)
        self.match(p, ['any', 'opencapture', 'span', 'closecapture'])
        off = p.dump()[1][2]
        # Csimple == 5
        self.assertEqual(self.captype(1, p), 5)
        self.assertEqual(off, 0)
    def testtypes(self):
        self.assertEqual(self.captype(0, Pattern.CapP()), 1)
        self.assertEqual(self.captype(0, Pattern.CapA(1)), 4)
        self.assertEqual(self.captype(1, Pattern.CapT(Pattern.Any(1))), 6)
        self.assertEqual(self.captype(1, Pattern.CapS(Pattern.Any(1))), 10)


class TestMatch(unittest.TestCase):
    def testdummy(self):
        p = Pattern.Dummy()
        self.assertEqual(p("aOmega"), 6)
    def testany(self):
        p = Pattern.Any(0)
        self.assertEqual(p("fred"), 0)
        p = Pattern.Any(1)
        self.assertEqual(p("fred"), 1)
        p = Pattern.Any(-1)
        self.assertEqual(p("fred"), None)
        p = Pattern.Any(-1)
        self.assertEqual(p(""), 0)
    def testmatch(self):
        p = Pattern.Match("fred")
        self.assertEqual(p("fred"), 4)
        self.assertEqual(p("freddy"), 4)
        self.assertEqual(p("jim"), None)
    def testfail(self):
        p = Pattern.Fail()
        self.assertEqual(p("jim"), None)
        self.assertEqual(p(""), None)

class TestCaptureRet(unittest.TestCase):
    def testpos(self):
        p = Pattern.Any(3) + Pattern.CapP() + Pattern.Any(2) + Pattern.CapP()
        self.assertEqual(p("abcdef"), [3, 5])
    def testarg(self):
        p = Pattern.CapA(1) + Pattern.CapA(3) + Pattern.CapA(2)
        self.assertEqual(p("abcdef", 1, "hi", None), [1, None, "hi"])

if __name__ == '__main__':
    unittest.main()
