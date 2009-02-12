import _cpeg, struct

code = [(_cpeg.Call, 2, 0),
 (_cpeg.Jump, 11, 0),
 (_cpeg.Char, 7, 'O'),
 (_cpeg.Choice, 6, 1),
 (_cpeg.Char, 0, 'm'),
 (_cpeg.Char, 0, 'e'),
 (_cpeg.Char, 0, 'g'),
 (_cpeg.Char, 0, 'a'),
 (_cpeg.Commit, 3, 0),
 (_cpeg.Any, 0, 1),
 (_cpeg.Jump, -8, 0),
 (_cpeg.Return, 0, 0),
 (_cpeg.End, 0, 0)]

instr = ''.join([struct.pack("iii28x", a, b, ord(c) if isinstance(c, str) else c)
                 for (a,b,c) in code])

print _cpeg.match(instr, "...Omega...")
