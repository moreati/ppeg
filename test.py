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
    yield
    sys.stdout = old

class TestBasic(unittest.TestCase):
    def testpat(self):
        p = Pattern()
        self.assert_(p is not None)
    def testdump(self):
        p = Pattern()
        with stdout(StringIO()):
            self.assertEqual(p.dump(), None)
    def testdummy(self):
        p = Pattern.Dummy()
        with stdout(StringIO()):
            self.assertEqual(p.dump(), None)
        self.assertEqual(p.match("aOmega"), 6)
    def testany(self):
        p = Pattern.Any(0)
        self.assertEqual(p.match("fred"), 0)
        p = Pattern.Any(1)
        self.assertEqual(p.match("fred"), 1)
        p = Pattern.Any(-1)
        self.assertRaises(IndexError, p.match, "fred")
        p = Pattern.Any(-1)
        self.assertEqual(p.match(""), 0)
    def testmatch(self):
        p = Pattern.Match("fred")
        self.assertEqual(p.match("fred"), 4)
        self.assertEqual(p.match("freddy"), 4)
        self.assertRaises(IndexError, p.match, "jim")
    def testfail(self):
        p = Pattern.Fail()
        self.assertRaises(IndexError, p.match, "jim")
        self.assertRaises(IndexError, p.match, "")

if __name__ == '__main__':
    unittest.main()
