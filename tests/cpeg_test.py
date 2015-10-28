import _cpeg, struct

code = [(_cpeg.Any, 0, 1),
        (_cpeg.Capture, 0, 2, 5),
        (_cpeg.Any, 0, 1),
        (_cpeg.End,)]
instr = ''.join([_cpeg.pack(*c) for c in code])
print _cpeg.match(instr, u"..")

code = [(_cpeg.Call, 2),
 (_cpeg.Jump, 11),
 (_cpeg.Char, 7, u'O'),
 (_cpeg.Choice, 6, 1),
 (_cpeg.Char, 0, u'm'),
 (_cpeg.Char, 0, u'e'),
 (_cpeg.Char, 0, u'g'),
 (_cpeg.Char, 0, u'a'),
 (_cpeg.Commit, 3),
 (_cpeg.Any, 0, 1),
 (_cpeg.Jump, -8),
 (_cpeg.Return,),
 (_cpeg.End,)]

instr = ''.join([_cpeg.pack(*c) for c in code])

print _cpeg.match(instr, u"...Omega...")
import time
txt = open("kjv10.txt").read().decode('ascii')
print "CPEG"
for i in range(10):
    start = time.clock()
    print _cpeg.match(instr, txt),
    print "%.2f sec" % (time.clock() - start)

print "String.find"
for i in range(10):
    start = time.clock()
    print txt.find("Omega"),
    print "%.2f sec" % (time.clock() - start)

print "re.find"
import re
r = re.compile(r"Omega")
for i in range(10):
    start = time.clock()
    m = r.search(txt)
    if m:
        print m.end(),
    else:
        print "Not found",
    print "%.2f sec" % (time.clock() - start)

