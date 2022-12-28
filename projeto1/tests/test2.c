#include "../fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {

    // Tests with a long string full of spaces and random numbers, 
    // and letters, if its copied correctly with tfs_copy_from_external_fs

    char *str_ext_file = "rea         huiohghj2    woi'fjnhdi       0nq35i "
                         "0easr  ++++  djiopvf mzxf'o zgmb'qite mndwihn ";
    char *path_copied_file = "/f1";
    char *path_src = "tests/file_to_copy2.txt";
    char buffer[100];

    assert(tfs_init(NULL) != -1);

    int f;
    ssize_t r;

    f = tfs_copy_from_external_fs(path_src, path_copied_file);
    assert(f != -1);

    f = tfs_open(path_copied_file, TFS_O_CREAT);
    assert(f != -1);

    // comparing both buffer and str_ext_file, it seems that everything is ok
    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str_ext_file));
    assert(!memcmp(buffer, str_ext_file, strlen(str_ext_file)));

    printf("Successful test.\n");

    return 0;
}
