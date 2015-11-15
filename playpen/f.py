from __future__ import print_function

import re
import sys
import timeit

import regex
import si_prefix

from _ppeg import Pattern as P

print('     subj length    loops       re    regex     ppeg')

for i in [       1,    2,    3,    4,    5,    6,    7,    8,    9, 
                10,   20,   30,   40,   50,   60,   70,   80,   90,
               100,  200,  300,  400,  500,  600,  700,  800,  900,
              1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,
             10000,
            100000,
               1e6, # Million
               1e7,
               1e8,
               1e9, # Billion
          ]:
    i = int(i)
    loops = max(1, 1000000/i)
    subject = 'a'*i

    print('{:>16n} {:>8} '.format(i, loops), sep='', end='')
    sys.stdout.flush()

    # re
    times = timeit.repeat("p.match('%s')" % subject,
                          setup="import re; p=re.compile('(a)*')",
                          repeat=3, number=loops)
    quickest = min(times)/loops
    print('{:>8} '.format(si_prefix.si_format(quickest, precision=1)), sep='', end='')
    sys.stdout.flush()

    # regex
    try:
        times = timeit.repeat("p.match('%s')" % subject,
                              setup="import regex; p=regex.compile('(a)*')",
                              repeat=3, number=loops)
    except regex.error:
        print('{:^8} '.format('error'), sep='', end='')
    else:
        quickest = min(times)/loops
        print('{:>8} '.format(si_prefix.si_format(quickest, precision=1)), sep='', end='')
    sys.stdout.flush()

    # ppeg
    times = timeit.repeat("p('%s')" % subject,
                          setup="import _ppeg; p=_ppeg.Pattern.Cap('a')**0",
                          repeat=3, number=loops)
    quickest = min(times)/loops
    print('{:>8} '.format(si_prefix.si_format(quickest, precision=1)), sep='', end='')
    sys.stdout.flush()

    print()
