/* Test for HAVE_FSEEKO */
#include <stdio.h>
int main()
{
    return fseeko(stdin, (off_t) 0, 0);
}
