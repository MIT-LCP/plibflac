/* Test for FLAC__HAS_X86INTRIN */
#ifdef _MSC_VER
# include <intrin.h>
#else
# include <x86intrin.h>
#endif
int main()
{
    return 0;
}
