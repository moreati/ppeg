from distutils.core import setup, Extension

setup (
    name = 'ppeg',
    version = '1.0',
    ext_modules = [Extension('_ppeg', ['_ppeg.c'])],
)

