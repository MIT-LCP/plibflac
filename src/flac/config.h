/* config.h for building libFLAC as part of plibflac */

/* Define if building universal (internal helper macro) */
//#cmakedefine AC_APPLE_UNIVERSAL_BUILD

/* Target processor is big endian. */
#define CPU_IS_BIG_ENDIAN PLIBFLAC_WORDS_BIGENDIAN

/* Target processor ARM64 */
//#cmakedefine FLAC__CPU_ARM64

/* Set FLAC__BYTES_PER_WORD to 8 (4 is the default) */
#define ENABLE_64_BIT_WORDS 1

/* define to align allocated memory on 32-byte boundaries */
//#cmakedefine FLAC__ALIGN_MALLOC_DATA

/* define if you have docbook-to-man or docbook2man */
//#cmakedefine FLAC__HAS_DOCBOOK_TO_MAN

/* define if you have the ogg library */
#define OGG_FOUND 0
#define FLAC__HAS_OGG OGG_FOUND

/* Set to 1 if <x86intrin.h> is available. */
//#cmakedefine01 FLAC__HAS_X86INTRIN

/* Set to 1 if <arm_neon.h> is available. */
//#cmakedefine01 FLAC__HAS_NEONINTRIN

/* Set to 1 if <arm_neon.h> contains A64 intrinsics */
//#cmakedefine01 FLAC__HAS_A64NEONINTRIN

/* define if building for Darwin / MacOS X */
//#cmakedefine FLAC__SYS_DARWIN

/* define if building for Linux */
//#cmakedefine FLAC__SYS_LINUX

/* define to enable use of AVX instructions */
//#cmakedefine WITH_AVX
//#ifdef WITH_AVX
//  #define FLAC__USE_AVX
//#endif

/* Define to the commit date of the current git HEAD */
//#cmakedefine GIT_COMMIT_DATE "@GIT_COMMIT_DATE@"

/* Define to the short hash of the current git HEAD */
//#cmakedefine GIT_COMMIT_HASH "@GIT_COMMIT_HASH@"

/* Define to the tag of the current git HEAD */
//#cmakedefine GIT_COMMIT_TAG "@GIT_COMMIT_TAG@"

/* Compiler has the __builtin_bswap16 intrinsic */
//#cmakedefine HAVE_BSWAP16

/* Compiler has the __builtin_bswap32 intrinsic */
//#cmakedefine HAVE_BSWAP32

/* Define to 1 if you have the <byteswap.h> header file. */
//#cmakedefine HAVE_BYTESWAP_H

/* define if you have clock_gettime */
//#cmakedefine HAVE_CLOCK_GETTIME

/* Define to 1 if you have the <cpuid.h> header file. */
//#cmakedefine HAVE_CPUID_H

/* Define to 1 if fseeko (and presumably ftello) exists and is declared. */
//#cmakedefine HAVE_FSEEKO

/* Define to 1 if you have the `getopt_long' function. */
//#cmakedefine HAVE_GETOPT_LONG

/* Define if you have the iconv() function and it works. */
//#cmakedefine HAVE_ICONV

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if you have <langinfo.h> and nl_langinfo(CODESET). */
//#cmakedefine HAVE_LANGINFO_CODESET

/* lround support */
#define HAVE_LROUND 1

/* Define to 1 if you have the <memory.h> header file. */
//#cmakedefine HAVE_MEMORY_H

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
//#cmakedefine HAVE_SYS_IOCTL_H

/* Define to 1 if you have the <sys/param.h> header file. */
//#cmakedefine HAVE_SYS_PARAM_H

/* Define to 1 if you have the <sys/stat.h> header file. */
//#cmakedefine HAVE_SYS_STAT_H

/* Define to 1 if you have the <sys/types.h> header file. */
//#cmakedefine HAVE_SYS_TYPES_H

/* Define to 1 if you have the <termios.h> header file. */
//#cmakedefine HAVE_TERMIOS_H

/* Define to 1 if typeof works with your compiler. */
//#cmakedefine HAVE_TYPEOF

/* Define to 1 if you have the <unistd.h> header file. */
//#cmakedefine HAVE_UNISTD_H

/* Define to 1 if you have the <x86intrin.h> header file. */
//#cmakedefine HAVE_X86INTRIN_H

/* Define as const if the declaration of iconv() needs const. */
//#cmakedefine ICONV_CONST

/* Define if debugging is disabled */
//#cmakedefine NDEBUG

/* Name of package */
//#cmakedefine PACKAGE

/* Define to the address where bug reports for this package should be sent. */
//#cmakedefine PACKAGE_BUGREPORT

/* Define to the full name of this package. */
//#cmakedefine PACKAGE_NAME

/* Define to the full name and version of this package. */
//#cmakedefine PACKAGE_STRING

/* Define to the one symbol short name of this package. */
//#cmakedefine PACKAGE_TARNAME

/* Define to the home page for this package. */
//#cmakedefine PACKAGE_URL

/* Define to the version of this package. */
#define PACKAGE_VERSION PLIBFLAC_FLAC_VERSION

/* The size of `off_t', as computed by sizeof. */
//#cmakedefine SIZEOF_OFF_T

/* The size of `void*', as computed by sizeof. */
//#cmakedefine SIZEOF_VOIDP

/* Enable extensions on AIX 3, Interix.  */
//#ifndef _ALL_SOURCE
//#define _ALL_SOURCE
//#endif

/* Enable GNU extensions on systems that have them.  */
//#ifndef _GNU_SOURCE
//#define _GNU_SOURCE
//#endif

//#ifndef _XOPEN_SOURCE
//#cmakedefine DODEFINE_XOPEN_SOURCE 500
//#ifdef DODEFINE_XOPEN_SOURCE
//#define _XOPEN_SOURCE DODEFINE_XOPEN_SOURCE
//#endif
//#endif

/* Enable threading extensions on Solaris.  */
//#ifndef _POSIX_PTHREAD_SEMANTICS
//#cmakedefine _POSIX_PTHREAD_SEMANTICS
//#endif
/* Enable extensions on HP NonStop.  */
//#ifndef _TANDEM_SOURCE
//#cmakedefine _TANDEM_SOURCE
//#endif
/* Enable general extensions on Solaris.  */
//#ifndef __EXTENSIONS__
//#cmakedefine DODEFINE_EXTENSIONS
//#ifdef DODEFINE_EXTENSIONS
//#define __EXTENSIONS__ DODEFINE_EXTENSIONS
//#endif
//#endif


/* Target processor is big endian. */
#define WORDS_BIGENDIAN PLIBFLAC_WORDS_BIGENDIAN

/* Enable large inode numbers on Mac OS X 10.5.  */
//#ifndef _DARWIN_USE_64_BIT_INODE
//# define _DARWIN_USE_64_BIT_INODE 1
//#endif

/* Number of bits in a file offset, on hosts where this is settable. */
//#ifndef _FILE_OFFSET_BITS
//# define _FILE_OFFSET_BITS 64
//#endif

/* Define to 1 to make fseeko visible on some hosts (e.g. glibc 2.2). */
//#ifndef _LARGEFILE_SOURCE
//# define _LARGEFILE_SOURCE
//#endif

/* Define for large files, on AIX-style hosts. */
//#cmakedefine _LARGE_FILES

/* Define to 1 if on MINIX. */
//#cmakedefine _MINIX

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
//#cmakedefine _POSIX_1_SOURCE

/* Define to 1 if you need to in order for `stat' and other things to work. */
//#cmakedefine _POSIX_SOURCE

/* Define to __typeof__ if your compiler spells it that way. */
//#cmakedefine typeof
