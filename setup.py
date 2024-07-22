#!/usr/bin/env python3

import platform
import re
import sys

from setuptools import Extension, setup

################################################################

with open('pyproject.toml') as f:
    _version = re.search('^version *= *"(.*?)"', f.read(), re.M)[1]
_define_macros = [('PLIBFLAC_VERSION', '"%s"' % _version)]

################################################################

_stable_abi = (3, 5)
if (platform.python_implementation() == 'CPython'
        and sys.version_info >= _stable_abi):
    _define_macros += [('Py_LIMITED_API', '0x%02x%02x0000' % _stable_abi)]
    _py_limited_api = True
    _bdist_wheel_options = {'py_limited_api': 'cp%d%d' % _stable_abi}
else:
    _py_limited_api = False
    _bdist_wheel_options = {}

################################################################

setup(
    name="plibflac",
    version=_version,
    package_dir={'': 'src'},
    packages=["plibflac"],
    ext_modules=[
        Extension(
            name="_plibflac",
            sources=["src/_plibflacmodule.c"],
            libraries=["FLAC"],
            define_macros=_define_macros,
            py_limited_api=_py_limited_api,
        ),
    ],
    options={
        'bdist_wheel': _bdist_wheel_options,
    },
)
