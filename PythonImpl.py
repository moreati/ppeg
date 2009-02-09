"""A test implementation of a PEG program builder in Python.

The PEG matcher needs to be in C for speed, but the instruction stream can be
built in Python and comprezssed into a C-style structure at the last minute,
just before matching. This allows for higher-level access to the instructions,
and hence more flexibility in experimenting with and implementing operations
and optimisations.
"""

# Opcodes
Opcodes = [
  "IAny", "IChar", "ISet", "ISpan",
  "IRet", "IEnd",
  "IChoice", "IJmp", "ICall", "IOpenCall",
  "ICommit", "IPartialCommit", "IBackCommit", "IFailTwice", "IFail",
  "IGiveup", "IFunc",
  "IFullCapture", "IEmptyCapture", "IEmptyCaptureIdx",
  "IOpenCapture", "ICloseCapture", "ICloseRunTime"
]

class Op(object):
    pass
Op = Op()

for n, op in enumerate(Opcodes):
    setattr(Op, op, n)

class Instruction(object):
    def __init__(self, op, aux = None, offset = 0):
        self.op = op
        self.aux = aux
        self.offset = offset
    def __str__(self):
        return "%s(%s,%s)" % (Opcodes[self.op], self.aux, self.offset)

def Fail():
    return [Instruction(Op.IFail)];

def Succeed():
    return []

def Any(n):
    # Breaking up into 255-chunks is implementation dependent. Maybe leave
    # this until converting to final form???
    if n == 0:
        return []
    if n > 0:
        ret = []
        while (n > 255):
            ret.append(Instruction(Op.IAny, 255))
            n -= 255
        ret.append(Instruction(Op.IAny, n))
        return ret
    else:
        ret = []
    return [Instruction(Op.IAny, n)]

def Str(s):
    return [Instruction(Op.IChar, c) for c in s]
