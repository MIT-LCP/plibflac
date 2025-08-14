/* Test for HAVE_PTHREAD */
#include <pthread.h>
int main()
{
    pthread_mutex_t m;
    pthread_mutex_init(&m, (pthread_mutexattr_t *) 0);
    return 0;
}
