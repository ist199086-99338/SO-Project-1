#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
    This file tests multiple threads that will be writting on the same file
*/
#define PATH "/testfile"
#define INPUT "Hello, SO teachers. Can we have 20 pls?"
#define THREAD_COUNT 100

void *wrapper_write(void *lol) {
    int f;

    if (lol != NULL)
        lol = NULL;

    // tfs_open will return -1 if the open file count is more then 20, which is
    // normal and this test takes that into account
    f = tfs_open(PATH, TFS_O_TRUNC);

    // if f == -1, then the file wasnt opened and we cant write to it
    if (f != -1)
        assert(tfs_write(f, INPUT, strlen(INPUT) + 1) != -1);

    if (f != -1)
        assert(tfs_close(f) != -1);

    return NULL;
}

int main() {

    char buffer[strlen(INPUT) + 1];

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

    // Read the file after all the write operations are done
    assert((f = tfs_open(PATH, TFS_O_START)) != -1);
    assert(tfs_read(f, buffer, strlen(INPUT) + 1) != -1);
    assert(tfs_close(f) != -1);

    // Check if the buffer equals the input of the threads
    assert(strcmp(buffer, INPUT) == 0);

    assert(tfs_destroy() != -1);

    printf("thread_test2: All good!\n");

    return 0;
}