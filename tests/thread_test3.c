#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 120

#define FILE_COUNT 4

char *input_files[] = {
    "tests/input1.txt",
    "tests/input2.txt",
    "tests/input3.txt",
    "tests/input4.txt",
};
char *tfs_files[] = {"/f1", "/f2", "/f3", "/f4"};

/* This test creates multiple files simultaneously and fill them up with
 * different content. Additionally, various writes are performed that may go
 * over blocks, making sure there is thread-safety when using tfs_write
 * over multiple data blocks. Finally, the contents of each file are read and
 * compared with the original files. */

void *write_to_file(void *input) {
    int file_id = *((int *)input);

    // first we set the respective files from within the lists 
    // made above depending on the number of the file_i
    // which works as an index for said lists
    char *input_file = input_files[file_id];    
    char *path = tfs_files[file_id];

    int f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    // copies the content of the respective input file 
    // to the newly created file, depending of the 
    // respective file_id selected
    int i = tfs_copy_from_external_fs(input_file, path);
    assert(i == 0);

    assert(tfs_close(f) == 0);
    return NULL;
}

void check_if_file_was_correctly_written(char *input_file, char *tfs_file) {
    FILE *fd = fopen(input_file, "r");
    assert(fd != NULL);

    char buffer_external[BUFFER_LEN];
    char buffer_tfs[BUFFER_LEN];
    // memset the buffers to 0 to clean the newly created buffers
    memset(buffer_external, 0, sizeof(buffer_external));
    memset(buffer_tfs, 0, sizeof(buffer_tfs));

    int f = tfs_open(tfs_file, 0);
    assert(f != -1);

    size_t bytes_read_external =
        fread(buffer_external, sizeof(char), BUFFER_LEN, fd);
    ssize_t bytes_read_tfs = tfs_read(f, buffer_tfs, BUFFER_LEN);

    // after reading for the first time, lets check if whats in the 
    // buffer_external is equal to whats in the buffer_tfs, to 
    // verify if its correctly written
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
}

int main() {
    assert(tfs_init(NULL) != -1);

    pthread_t tid[FILE_COUNT];
    int file_id[FILE_COUNT];

    for (int i = 0; i < FILE_COUNT; ++i) {
        // adds the respective file id to the newly created list
        // and then proceeds to create threads to do the write_to_file void function
        file_id[i] = i;
        assert(pthread_create(&tid[i], NULL, write_to_file,
                              (void *)(&file_id[i])) == 0);
    }

    for (int i = 0; i < FILE_COUNT; ++i) {
        assert(pthread_join(tid[i], NULL) == 0);
        // then we wait for the respective threads
    }

    for (int i = 0; i < FILE_COUNT; ++i) {
        check_if_file_was_correctly_written(input_files[i], tfs_files[i]);
        // and then thanks to the tfs_file list that we created earlier,
        // it will help us to check if the respective file was correctly written
    }

    printf("Successful test.\n");
    assert(tfs_destroy() == 0);
    return 0;
}


