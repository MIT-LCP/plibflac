/* Test for FLAC__HAS_A64NEONINTRIN */
#include <arm_neon.h>
int main()
{
    float64x2_t tmp;
    tmp = vdupq_n_f64(0.0f);
    return 0;
}
