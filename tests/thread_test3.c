#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
    This file tests that only 20 files can be open at the same time,
	with multiple threads testing that limit
*/
#define PATH "/testfile"
#define THREAD_COUNT 10000

int count = 0;

void *wrapper_write(void *lol) {
    int f;

	if (lol != NULL)
		lol = NULL;

    // tfs_open can return -1 if more than 20 files are open, which is normal
    f = tfs_open(PATH, TFS_O_TRUNC);

    if(f != -1)
        count++;

    return NULL;
}

int main() {

    assert(tfs_init() != -1);

    int f;
    // Create the file for testing
    assert((f = tfs_open(PATH, TFS_O_CREAT)) != -1);
    assert(tfs_close(f) != -1);

    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        assert(pthread_create(&threads[i], NULL, wrapper_write, NULL) != -1);
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }

	// Only 20 files should have been opened by the threads
	assert(count == 20);

    assert(tfs_destroy() != -1);

    printf("thread_test3: All good!\n");

    return 0;
}