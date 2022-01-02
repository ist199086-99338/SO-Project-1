#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>

#define init_lock( A ) pthread_rwlock_init(A, NULL)
#define read_lock( A ) pthread_rwlock_rdlock(A)
#define write_lock( A ) pthread_rwlock_wrlock(A)
#define unlock( A ) pthread_rwlock_unlock(A)
#define destroy_lock( A ) pthread_rwlock_destroy(A)

#endif