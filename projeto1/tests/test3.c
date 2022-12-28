#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Sym Link Test

char const target_path1[] = "/f1";
char const link_path1[] = "/l1";

int main() {
    assert(tfs_init(NULL) != -1);

    int f = tfs_open(target_path1, TFS_O_CREAT);
    assert(f != -1);
    assert(tfs_close(f) != -1);

    // a symbolic link is created and then we search for him in the system
    // to verify if he was really created
    assert(tfs_sym_link(target_path1, link_path1) != -1);

    int i = tfs_open(link_path1, 0);
    assert(i != -1);
    assert(tfs_close(i) != -1);

    assert(tfs_unlink(target_path1) != 1);

    // now we erased the target associated with the symbolic link
    // so we will see now as well if the sistem can open the target file
    // it should not
    int j = tfs_open(target_path1, 0);
    assert(j == -1);

    assert(tfs_destroy() != -1);
    printf("Successful test.\n");

    return 0;
}