#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 120
#define THREAD_NUM 20
#define INPUT_FILE "tests/input1.txt"
#define TFS_FILE "/f1"

/* This test fills a file with medium size content, and then tries to read from it
 * simultaneously in multiple threads, comparing the content with the original
 * file */

void write_fn() {
    int f = tfs_open(TFS_FILE, TFS_O_CREAT | TFS_O_TRUNC);
    assert(f != -1);

    // copies the content of the input file to the newly created file
    int i = tfs_copy_from_external_fs(INPUT_FILE, TFS_FILE);
    assert(i == 0);

    assert(tfs_close(f) == 0);
}

void *read_fn(void *input) {
    (void)input; // ignore parameter

    FILE *fd = fopen(INPUT_FILE, "r");
    assert(fd != NULL);

    char buffer_external[BUFFER_LEN];
    char buffer_tfs[BUFFER_LEN];
    // memset the buffers to 0 to clean the newly created buffers 
    memset(buffer_external, 0, sizeof(buffer_external)); 
    memset(buffer_external, 0, sizeof(buffer_tfs));

    int f = tfs_open(TFS_FILE, 0);
    assert(f != -1);

    // after opening the file, lets check if whats read in the fd file is 
    // equal to whats read in the file opened with tfs_read
    size_t bytes_read_external =
        fread(buffer_external, sizeof(char), BUFFER_LEN, fd);
    ssize_t bytes_read_tfs = tfs_read(f, buffer_tfs, BUFFER_LEN);
    
    while (bytes_read_external > 0 && bytes_read_tfs > 0) {
        assert(strncmp(buffer_external, buffer_tfs, BUFFER_LEN) == 0);
        bytes_read_external =
            fread(buffer_external, sizeof(char), BUFFER_LEN, fd);
        bytes_read_tfs = tfs_read(f, buffer_tfs, BUFFER_LEN);
    }

    // check if both files reached the end
    assert(bytes_read_external == 0);
    assert(bytes_read_tfs == 0);

    assert(tfs_close(f) == 0);
    assert(fclose(fd) == 0);

    return NULL;
}

int main() {
    assert(tfs_init(NULL) != -1);

    pthread_t tid[THREAD_NUM];

    // this function will write and check the contents of 
    // its content to initiate the test
    write_fn();

    for (int i = 0; i < THREAD_NUM; i++) {
        // then we create the new threads to use the tfs_copy_from_external 
        // function which will check if the content of the files is correct
        assert(pthread_create(&tid[i], NULL, read_fn, NULL) == 0);
    }

    for (int i = 0; i < THREAD_NUM; i++) {
        assert(pthread_join(tid[i], NULL) == 0);
        // then we wait for the respective threads
    }

    assert(tfs_destroy() == 0);
    printf("Successful test.\n");

    return 0;
}


