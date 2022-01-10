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
#define INPUT                                                                  \
    "(Read with an Indian accent) Hello, SO teachers. Can we have 20 pls?"
#define THREAD_COUNT 20

void *wrapper_write(void *lol) {
    int f;

    assert((f = tfs_open(PATH, TFS_O_TRUNC)) != -1);

    if (lol == NULL)
        lol = NULL;

    assert(tfs_write(f, INPUT, strlen(INPUT) + 1) != -1);

    assert(tfs_close(f) != -1);

    return NULL;
}

int main() {

    char *input =
        "(Read with an Indian accent) Hello, SO teachers. Can we have 20 pls?";
    char buffer[strlen(input) + 1];

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
    assert(tfs_read(f, buffer, strlen(input) + 1) != -1);
    assert(tfs_close(f) != -1);

    // Check if the buffer equals the input of the threads
    assert(strcmp(buffer, input) == 0);

    assert(tfs_destroy() != -1);

    printf("thread_test2: All good!\n");

    return 0;
}