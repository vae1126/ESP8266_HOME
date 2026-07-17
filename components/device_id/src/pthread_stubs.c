#include <stdint.h>

int pthread_cond_init(void *cond, const void *attr) { return 0; }
int pthread_cond_broadcast(void *cond) { return 0; }
int pthread_cond_wait(void *cond, void *mutex) { return 0; }
int pthread_cond_destroy(void *cond) { return 0; }
int pthread_cond_signal(void *cond) { return 0; }
int pthread_cond_timedwait(void *cond, void *mutex, const void *abstime) { return 0; }
