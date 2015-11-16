====
PPeg
====

*PPeg* is a pattern matching library for Python, based on
`Parsing Expression Grammars`_ (PEGs).
It's a port of the `LPeg`_ library from Lua.

.. warning::
    PPeg is alpha software, it's not ready for general use.
    There are bugs, and they will crash/coredeump your Python process.

.. warning::
    PPeg is experimental. The API and semantics are not stable.
    Future releases will break backward compatibility, without warning.

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

Limitations
===========

- PPeg only supports CPython 2.6 and 2.7.
- PPeg doesn't support Unicode, only byte strings can be matched or searched.
  This is closely tied to how Lua and LPeg handle strings.
- PPeg is untested on any platform except 64-bit Linux.
- Bugs, lots of bugs.
