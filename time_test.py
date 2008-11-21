import timeit
setup="""
from _ppeg import Pattern

text = open('kjv10.txt').read()

pat = Pattern.Dummy()
pat.dump()
"""

t = timeit.Timer(stmt='print pat(text)', setup=setup)
N=5
print "Time taken: %.2f sec" % (t.timeit(N)/float(N))

