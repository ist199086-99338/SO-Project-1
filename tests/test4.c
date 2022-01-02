#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 256

typedef struct {
    int f;
    char *output;
    size_t len;
} Args;

void *wrapper_read(void *args) {
    tfs_read(((Args *)args)->f, ((Args *)args)->output, ((Args *)args)->len);

    return NULL;
}

int main() {
    char *input =
        "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Aenean "
        "commodo ligula eget dolor. Aenean massa. Cum sociis natoque penatibus "
        "et magnis dis parturient montes, nascetur ridiculus mus. Donec quam "
        "felis, ultricies nec, pellentesque eu, pretium quis, sem. Nulla "
        "consequat massa quis enim. Donec pede justo, fringilla vel, aliquet "
        "nec, vulputate eget, arcu. In enim justo, rhoncus ut, imperdiet a, "
        "venenatis vitae, justo. Nullam dictum felis eu pede mollis pretium. "
        "Integer tincidunt. Cras dapibus. Vivamus elementum semper nisi. "
        "Aenean vulputate eleifend tellus. Aenean leo ligula, porttitor eu, "
        "consequat vitae, eleifend ac, enim. Aliquam lorem ante, dapibus in, "
        "viverra quis, feugiat a, tellus. Phasellus viverra nulla ut metus "
        "varius laoreet. Quisque rutrum. Aenean imperdiet. Etiam ultricies "
        "nisi vel augue. Curabitur ullamcorper ultricies nisi. Nam eget dui. "
        "Etiam rhoncus. Maecenas tempus, tellus eget condimentum rhoncus, sem "
        "quam semper libero, sit amet adipiscing sem neque sed ipsum. Nam quam "
        "nunc, blandit vel, luctus pulvinar, hendrerit id, lorem. Maecenas nec "
        "odio et ante tincidunt tempus. Donec vitae sapien ut libero venenatis "
        "faucibus. Nullam quis ante. Etiam sit amet orci eget eros faucibus "
        "tincidunt. Duis leo. Sed fringilla mauris sit amet nibh. Donec "
        "sodales sagittis magna. Sed consequat, leo eget bibendum sodales, "
        "augue velit cursus nunc, quis gravida magna mi a libero. Fusce "
        "vulputate eleifend sapien. Vestibulum purus quam, scelerisque ut, "
        "mollis sed, nonummy id, metus. Nullam accumsan lorem in dui. Cras "
        "ultricies mi eu turpis hendrerit fringilla. Vestibulum ante ipsum "
        "primis in faucibus orci luctus et ultrices posuere cubilia Curae; In "
        "ac dui quis mi consectetuer lacinia. Nam pretium turpis et arcu. Duis "
        "arcu tortor, suscipit eget, imperdiet nec, imperdiet iaculis, ipsum. "
        "Sed aliquam ultrices mauris. Integer ante arcu, accumsan a, "
        "consectetuer eget, posuere ut, mauris. Praesent adipiscing. Phasellus "
        "ullamcorper ipsum rutrum nunc. Nunc nonummy metus. Vestibulum ";

    char output1[strlen(input) + 1];
    char output2[strlen(input) + 1];
    char *path = "/f1";

    assert(tfs_init() != -1);

    int f;
    ssize_t r;

    f = tfs_open(path, TFS_O_CREAT);

    r = tfs_write(f, input, strlen(input) + 1);
    assert(r == strlen(input) + 1);

    r = tfs_close(f);

    f = tfs_open(path, 0);

    Args args1 = {f, output1, strlen(input) + 1};
    Args args2 = {f, output2, strlen(input) + 1};

    pthread_t tid1, tid2;
    r = pthread_create(&tid1, NULL, wrapper_read, (void *)&args1);
    r = pthread_create(&tid2, NULL, wrapper_read, (void *)&args2);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    assert(strcmp(output1, output2) == 0);

    assert(tfs_close(f) != -1);

    printf("Successful test.\n");

    return 0;
}
