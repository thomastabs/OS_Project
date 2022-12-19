#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_NAME_LEN 10
#define THREAD_NUM 2
#define FILES_TO_CREATE_PER_THREAD 10

/* This test creates as many files as possible in order to test if
 * inumbers are assigned correctly. */

void *create_file(void *arg) {
    int file_i = *((int *)arg);

    for (int i = 0; i < FILES_TO_CREATE_PER_THREAD; i++) {
        char path[MAX_NAME_LEN] = {'/'};
        sprintf(path + 1, "%d", file_i + i);

        // opens a file
        int f = tfs_open(path, TFS_O_CREAT);
        assert(f != -1);

        // write the file name to the file, so we can test if two files got the
        // same inode
        assert(tfs_write(f, path, strlen(path) + 1) == strlen(path) + 1);

        // and then we close it
        assert(tfs_close(f) != -1);
    }
    return NULL;
}

int main() {
    pthread_t tid[THREAD_NUM];
    assert(tfs_init(NULL) == 0);
    int table[THREAD_NUM];

    for (int i = 0; i < THREAD_NUM; ++i) {
        table[i] = i * FILES_TO_CREATE_PER_THREAD + 1;
    }

    for (int i = 0; i < THREAD_NUM; ++i) {
        // if is not possible to create a new thread, then it exits and ends the test 
        if (pthread_create(&tid[i], NULL, create_file, &table[i]) != 0) {
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < THREAD_NUM; ++i) {
        // then we make them wait for the respective thread
        pthread_join(tid[i], NULL);
    }

    // Check if files have the correct content
    for (int i = 0; i < THREAD_NUM * FILES_TO_CREATE_PER_THREAD; ++i) {
        char path[MAX_NAME_LEN] = {'/'};
        sprintf(path + 1, "%d", i + 1);

        int f = tfs_open(path, 0);
        assert(f != -1);

        // we then check if the buffer and the path attributes are equal
        char buffer[MAX_NAME_LEN];
        assert(tfs_read(f, buffer, MAX_NAME_LEN) != -1);

        assert(strcmp(path, buffer) == 0);

        // after checking it we close once more the respective file 
        assert(tfs_close(f) == 0);
    }

    tfs_destroy();
    printf("Successful test.\n");

    return 0;
}


