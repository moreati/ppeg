Changelog
=========

0.9.4 (2015-11-15)
------------------

* Added support for multiple arguments to Pattern.CapC(arg, ...) aka
  constant capture
* Added initial support for Pattern.CapT(p) (table capture) with
  positional/unnamed captures
* Added initial support for Pattern.CapRT(p, func) (match-time capture)
* Fixed Pattern.CapS(p) (substitution captures) when the pattern has no
  captures
* Added more error checking
* Added dynamic capture stack. Patterns can now return thousands of captures,
  depending on available RAM

0.9.3 (2015-11-05)
------------------

* Added support for Pattern.Cap(n) and Pattern.Cap(s),
  as shortcuts for Pattern.Cap(Pattern(n)) and Pattern.Cap(Pattern(s))
* Fixed uninitialized memory in Pattern objects
* Fixed matching of Pattern(-n) for n > 255
* Fixed memory leaks found with clang
* Fixed some cases when combining two patterns with p1 | p2
* Added Tox configutation for running the test suite under Python 2.6 & 2.7
* Moved the test suite to tests/*.py
* Ported more of LPeg's `test.lua` test suite

0.9.2 (2015-10-28)
------------------

* Moved source repository from Bitbucket to Github
* Added experimental warning to the README
* Added an example to the README

0.9.1 (2015-10-27)
------------------

* Added README, CHANGELOG, AUTHORS
* New maintainer: Alex Willmer
* Changed from distutils to setuptools

0.9 (2015-10-27)
----------------

* First release on PyPI
