#include "fs/operations.h"
#include <assert.h>
#include <string.h>

int main() {
    char *str = "123456789";
    char *path = "/f1";
    char *read = "";

    assert(tfs_init() != -1);

    int f;
    ssize_t r;

    f = tfs_open(path, TFS_O_CREAT);

    r = tfs_write(f, str, strlen(str));
    assert(r == strlen(str));

    r = tfs_close(f);

    f = tfs_open(path, 0);

    r = tfs_read(f, read, strlen(str));
    assert(r == strlen(str));
    assert(strcmp(str, read) == 0);
    printf("%s == %s\n", str, read);

    assert(tfs_close(f) != -1);

    printf("Successful test.\n");

    return 0;
}
