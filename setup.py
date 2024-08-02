#!/usr/bin/env python3

import os
import platform
import re
import sys

from setuptools import Extension, setup

################################################################

with open('pyproject.toml') as f:
    _version = re.search('^version *= *"(.*?)"', f.read(), re.M).group(1)
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


def _flac_options():
    # To use a copy of libFLAC that is already installed, set the
    # environment variable FLAC_CFLAGS to the list of compiler flags
    # and FLAC_LIBS to the list of linker flags.  You can determine
    # the appropriate flags using 'pkg-config'.

    cflags = os.environ.get('FLAC_CFLAGS', '')
    libs = os.environ.get('FLAC_LIBS', '')
    if libs:
        return {
            'sources': [],
            'include_dirs': [],
            'define_macros': [],
            'extra_compile_args': cflags.split(),
            'extra_link_args': libs.split(),
        }

    # If FLAC_LIBS is undefined, we'll compile and link with the copy
    # of libFLAC included in this distribution.

    pkgdir = 'flac'

    sources = []
    for f in os.listdir(os.path.join(pkgdir, 'src', 'libFLAC')):
        if f.endswith('.c') and not f.startswith('ogg_'):
            sources.append(os.path.join(pkgdir, 'src', 'libFLAC', f))

    include_dirs = [
        os.path.join('src', 'flac'),
        os.path.join(pkgdir, 'include'),
        os.path.join(pkgdir, 'src', 'libFLAC', 'include'),
    ]

    with open(os.path.join(pkgdir, 'CMakeLists.txt')) as f:
        version = re.search(r'\bproject\(FLAC\s+VERSION\s+([^\s\)]+)',
                            f.read()).group(1)

    # Additional preprocessor definitions required by libFLAC are
    # found in src/flac/config.h (to avoid conflicting with
    # definitions in Python.h.)

    define_macros = [
        ('HAVE_CONFIG_H', '1'),
        ('PLIBFLAC_FLAC_VERSION', '"%s"' % version),
        ('PLIBFLAC_WORDS_BIGENDIAN', str(int(sys.byteorder == 'big'))),
    ]

    # On most *nix platforms, we must use -fvisibility=hidden to
    # prevent the internal libFLAC from conflicting with any shared
    # libraries.  This shouldn't be necessary for Windows, and may not
    # be supported by Windows compilers.
    extra_compile_args = []
    if os.name != 'nt':
        extra_compile_args += ['-fvisibility=hidden']

    return {
        'sources': sources,
        'include_dirs': include_dirs,
        'define_macros': define_macros,
        'extra_compile_args': extra_compile_args,
        'extra_link_args': [],
    }


_flac = _flac_options()

################################################################

setup(
    name="plibflac",
    version=_version,
    package_dir={'': 'src'},
    packages=["plibflac"],
    ext_modules=[
        Extension(
            name="_plibflac",
            sources=[
                'src/_plibflacmodule.c',
                *_flac['sources'],
            ],
            define_macros=[
                *_define_macros,
                *_flac['define_macros'],
            ],
            include_dirs=_flac['include_dirs'],
            extra_compile_args=_flac['extra_compile_args'],
            extra_link_args=_flac['extra_link_args'],
            py_limited_api=_py_limited_api,
        ),
    ],
    options={
        'bdist_wheel': _bdist_wheel_options,
    },
)
