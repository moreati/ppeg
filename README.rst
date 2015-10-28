====
PPeg
====

*PPeg* is a pattern matching library for Python, based on
`Parsing Expression Grammars`_ (PEGs).
It's a port of the `LPeg`_ library from Lua.

.. warning::
    PPeg is still experimental. The API and semantics are not stable.

.. _Parsing Expression Grammars: https://en.wikipedia.org/wiki/Parsing_expression_grammar
.. _LPeg: http://www.inf.puc-rio.br/~roberto/lpeg/

Usage
=====

Unlike the ``re`` module [#]_, PPeg patterns can handle balanced sequences

.. code:: python

    >>> from _ppeg import Pattern as P
    >>> pattern = P.Grammar('(' + ( (P(1)-P.Set('()')) | P.Var(0) )**0 + ')')
    >>> pattern('(foo(bar()baz))').pos
    15
    >>> pattern('(foo(bar(baz)').pos
    -1
    >>> capture = P.Cap(pattern)
    >>> capture('(foo(bar()baz))').captures
    ['(foo(bar()baz))']

This example corresponds roughly to the following LPeg example

.. code:: lua

    > lpeg = require "lpeg"
    > pattern = lpeg.P{ "(" * ((1 - lpeg.S"()") + lpeg.V(1))^0 * ")" }
    > pattern:match("(foo(bar()baz))") -- Lua indexes begin at 1
    16
    > pattern:match("(foo(bar(baz)")
    nil
    > capture = lpeg.C(pattern)
    > capture:match("(foo(bar()baz))")
    "(foo(bar()baz))"

.. [#] Some regular expression implementations (e.g. PCRE_, regex_)
   support `recursive patterns`_, which can match balanced sequences.

.. _pcre: http://www.pcre.org/
.. _regex: https://pypi.python.org/pypi/regex
.. _recursive patterns: http://www.regular-expressions.info/recurse.html

Modules
=======
- _cpeg.c
- _ppeg.c
    - includes lpeg.c
