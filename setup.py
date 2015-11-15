#!/usr/bin/env python

import io
import os

from setuptools import setup, Extension


def read(fname, encoding='utf-8'):
    here = os.path.dirname(__file__)
    with io.open(os.path.join(here, fname), encoding=encoding) as f:
        return f.read()


setup (
    name='PPeg',
    version='0.9.4',
    description="A Python port of Lua's LPeg pattern matching library",
    long_description=read('README.rst'),
    url='https://github.com/moreati/ppeg',

    author='Alex Willmer',
    author_email='alex@moreati.org.uk',

    license='MIT',

    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 2 :: Only',
        'Topic :: Text Processing :: General',
    ],

    keywords='parsing peg grammar regex',

    ext_modules = [Extension('_ppeg', ['_ppeg.c', 'lpeg.c']),
                   Extension('_cpeg', ['_cpeg.c'])],
    py_modules=[
        'PythonImpl',
        'pegmatcher',
    ],
)

