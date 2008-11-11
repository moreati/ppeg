import unittest
import _ppeg

class TestBasic(unittest.TestCase):
    def testpat(self):
        p = _ppeg.Pattern()
        self.assert_(p is not None)
    def testdump(self):
        p = _ppeg.Pattern()
        self.assertEqual(p.dump(), None)
    def testdummy(self):
        p = _ppeg.Pattern()
        p.setdummy()
        self.assertEqual(p.dump(), None)
        self.assertEqual(p.match("aOmega"), 6)

if __name__ == '__main__':
    unittest.main()
