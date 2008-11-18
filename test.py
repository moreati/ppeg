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

if __name__ == '__main__':
    unittest.main()
