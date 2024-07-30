#!/usr/bin/env python3

import os
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
        os.path.join(pkgdir, 'include'),
        os.path.join(pkgdir, 'src', 'libFLAC', 'include'),
    ]

    with open(os.path.join(pkgdir, 'CMakeLists.txt')) as f:
        version = re.search(r'\bproject\(FLAC\s+VERSION\s+([^\s\)]+)',
                            f.read())[1]

    # Below is the complete list of macros from config.cmake.h.in.
    # Most of these are not relevant for libFLAC.  A few of them
    # control optimizations, which we ought to support if running on
    # that CPU (but the library should work without them.)  We assume
    # all relevant compilers support C99.  We don't care about
    # largefile/fseeko because all I/O goes through callbacks.

    define_macros = [
        # AC_APPLE_UNIVERSAL_BUILD  (unused)
        ('CPU_IS_BIG_ENDIAN', str(int(sys.byteorder == 'big'))),
        # FLAC__CPU_ARM64
        ('ENABLE_64_BIT_WORDS', '1'),
        # FLAC__ALIGN_MALLOC_DATA
        # FLAC__HAS_DOCBOOK_TO_MAN  (unused)
        ('OGG_FOUND', '0'),
        ('FLAC__HAS_OGG', '0'),
        # FLAC__HAS_X86INTRIN
        # FLAC__HAS_NEONINTRIN
        # FLAC__HAS_A64NEONINTRIN
        # FLAC__SYS_DARWIN          (unused)
        # FLAC__SYS_LINUX           (unused)
        # WITH_AVX
        # GIT_COMMIT_DATE
        # GIT_COMMIT_HASH
        # GIT_COMMIT_TAG
        # HAVE_BSWAP16
        # HAVE_BSWAP32
        # HAVE_BYTESWAP_H
        # HAVE_CLOCK_GETTIME        (unused)
        # HAVE_CPUID_H
        # HAVE_FSEEKO
        # HAVE_GETOPT_LONG          (unused)
        # HAVE_ICONV                (unused)
        ('HAVE_INTTYPES_H', '1'),
        # HAVE_LANGINFO_CODESET     (unused)
        ('HAVE_LROUND', '1'),
        # HAVE_MEMORY_H             (unused)
        ('HAVE_STDINT_H', '1'),
        ('HAVE_STDLIB_H', '1'),
        ('HAVE_STRING_H', '1'),
        # HAVE_SYS_IOCTL_H          (unused)
        # HAVE_SYS_PARAM_H
        # HAVE_SYS_STAT_H           (unused)
        # HAVE_SYS_TYPES_H          (unused)
        # HAVE_TERMIOS_H            (unused)
        # HAVE_TYPEOF               (unused)
        # HAVE_UNISTD_H             (unused)
        # HAVE_X86INTRIN_H          (unused)
        # ICONV_CONST               (unused)
        # NDEBUG
        # PACKAGE                   (unused)
        # PACKAGE_BUGREPORT         (unused)
        # PACKAGE_NAME              (unused)
        # PACKAGE_STRING            (unused)
        # PACKAGE_TARNAME           (unused)
        # PACKAGE_URL               (unused)
        ('PACKAGE_VERSION', '"%s"' % version),
        # SIZEOF_OFF_T              (unused)
        # SIZEOF_VOIDP              (unused)
        # _ALL_SOURCE               (unused)
        # _GNU_SOURCE               (unused)
        # DODEFINE_XOPEN_SOURCE     (unused)
        # _XOPEN_SOURCE             (unused)
        # _POSIX_PTHREAD_SEMANTICS  (unused)
        # _TANDEM_SOURCE            (unused)
        # DODEFINE_EXTENSIONS       (unused)
        # __EXTENSIONS__            (unused)
        ('WORDS_BIGENDIAN', str(int(sys.byteorder == 'big'))),
        # _DARWIN_USE_64_BIT_INODE  (unused)
        # _FILE_OFFSET_BITS         (unused)
        # _LARGEFILE_SOURCE         (unused)
        # _LARGE_FILES              (unused)
        # _MINIX                    (unused)
        # _POSIX_1_SOURCE           (unused)
        # _POSIX_SOURCE             (unused)
        # typeof                    (unused)
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
