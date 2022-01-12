#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>

#define init_rwlock(A) pthread_rwlock_init(A, NULL)
#define init_mlock(A) pthread_mutex_init(A, NULL)
#define read_lock(A) pthread_rwlock_rdlock(A)
#define write_lock(A) pthread_rwlock_wrlock(A)
#define rw_unlock(A) pthread_rwlock_unlock(A)
#define destroy_rwlock(A) pthread_rwlock_destroy(A)
#define destroy_mlock(A) pthread_mutex_destroy(A)
#define mutex_lock(A) pthread_mutex_lock(A)
#define mutex_unlock(A) pthread_mutex_unlock(A)

#endif