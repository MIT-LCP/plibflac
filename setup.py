#!/usr/bin/env python3

import copy
import os
import platform
import re
import sys
import sysconfig

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

################################################################

with open('pyproject.toml') as f:
    _version = re.search('^version *= *"(.*?)"', f.read(), re.M).group(1)
_define_macros = [('PLIBFLAC_VERSION', '"%s"' % _version)]

################################################################

_stable_abi = (3, 5)
if (platform.python_implementation() == 'CPython'
        and not sysconfig.get_config_var('Py_GIL_DISABLED')
        and sys.version_info >= _stable_abi):
    _define_macros += [('Py_LIMITED_API', '0x%02x%02x0000' % _stable_abi)]
    _py_limited_api = True
    _bdist_wheel_options = {'py_limited_api': 'cp%d%d' % _stable_abi}
else:
    _py_limited_api = False
    _bdist_wheel_options = {}

################################################################


def _flac_options(compiler, build_temp):
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

    if os.name == 'nt':
        sources.append(os.path.join(pkgdir, 'src', 'share',
                                    'win_utf8_io', 'win_utf8_io.c'))

    with open(os.path.join(pkgdir, 'CMakeLists.txt')) as f:
        version = re.search(r'\bproject\(FLAC\s+VERSION\s+([^\s\)]+)',
                            f.read()).group(1)

    # Additional preprocessor definitions required by libFLAC are
    # found in src/flac/config.h (to avoid conflicting with
    # definitions in Python.h.)

    define_macros = [
        ('HAVE_CONFIG_H', '1'),
        ('FLAC__NO_DLL', '1'),
        ('PLIBFLAC_FLAC_VERSION', '"%s"' % version),
        ('PLIBFLAC_WORDS_BIGENDIAN', str(int(sys.byteorder == 'big'))),
    ]

    # Test running the compiler to check for optional system features.

    def try_compile(src_name, extra_compile_args=[], trap_errors=True):
        src_path = os.path.join('src', 'conf', src_name)
        assert os.path.isfile(src_path), "%s not found" % src_path
        try:
            compiler.compile([src_path], output_dir=build_temp,
                             extra_postargs=extra_compile_args)
            return True
        except Exception:
            if trap_errors:
                return False
            raise

    # Check that the compiler works.
    try_compile('conftest.c', trap_errors=False)

    # On most *nix platforms, we must use -fvisibility=hidden to
    # prevent the internal libFLAC from conflicting with any shared
    # libraries.
    extra_compile_args = []
    if try_compile('conftest.c', ['-fvisibility=hidden']):
        extra_compile_args += ['-fvisibility=hidden']

    if try_compile('conftest_fseeko.c'):
        define_macros += [('PLIBFLAC_HAVE_FSEEKO', '1')]
    if try_compile('conftest_cpuid_h.c'):
        define_macros += [('PLIBFLAC_HAVE_CPUID_H', '1')]

    machine = platform.machine().lower()
    if re.match('i[3-6]86', machine):
        define_macros += [('FLAC__CPU_IA32', '1'),
                          ('FLAC__USE_AVX', '1'),
                          ('FLAC__ALIGN_MALLOC_DATA', '1')]
        if try_compile('conftest_x86intrin.c'):
            define_macros += [('FLAC__HAS_X86INTRIN', '1')]
    if re.match('x86_64|amd64', machine):
        define_macros += [('FLAC__CPU_X86_64', '1'),
                          ('FLAC__HAS_X86INTRIN', '1'),
                          ('FLAC__USE_AVX', '1'),
                          ('FLAC__ALIGN_MALLOC_DATA', '1')]
        if try_compile('conftest_x86intrin.c'):
            define_macros += [('FLAC__HAS_X86INTRIN', '1')]
    if re.match('arm64|aarch64', machine):
        define_macros += [('FLAC__CPU_ARM64', '1')]
        if try_compile('conftest_a64neonintrin.c'):
            define_macros += [('FLAC__HAS_NEONINTRIN', '1'),
                              ('FLAC__HAS_A64NEONINTRIN', '1')]
        if try_compile('conftest_neonintrin.c'):
            define_macros += [('FLAC__HAS_NEONINTRIN', '1')]

    return {
        'sources': sources,
        'include_dirs': include_dirs,
        'define_macros': define_macros,
        'extra_compile_args': extra_compile_args,
        'extra_link_args': [],
    }


class custom_build_ext(build_ext):
    def build_extension(self, ext):
        ext = copy.copy(ext)
        flac = _flac_options(self.compiler, self.build_temp)
        for key, value in flac.items():
            setattr(ext, key, getattr(ext, key) + value)

        super().build_extension(ext)


################################################################

setup(
    name="plibflac",
    version=_version,
    package_dir={'': 'src'},
    packages=["plibflac"],
    ext_modules=[
        Extension(
            name="_plibflac",
            sources=['src/_plibflacmodule.c'],
            define_macros=_define_macros,
            py_limited_api=_py_limited_api,
        ),
    ],
    cmdclass={
        'build_ext': custom_build_ext,
    },
    options={
        'bdist_wheel': _bdist_wheel_options,
    },
)
