from setuptools import setup, Extension

setup (
    name='PPeg',
    version='0.9',
    description="A Python port of Lua's LPeg pattern matching library",
    url='https://bitbucket.org/pmoore/ppeg',

    author='Paul Moore',

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

