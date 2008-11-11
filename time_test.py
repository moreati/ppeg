import timeit
setup="""
from _ppeg import Pattern

text = open('kjv10.txt').read()

pat = Pattern()
pat.setdummy()
pat.dump()
"""

t = timeit.Timer(stmt='print pat.match(text)', setup=setup)
N=5
print "Time taken: %.2f sec" % (t.timeit(N)/float(N))

