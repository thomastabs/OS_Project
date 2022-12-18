#include "fs/operations.h"
#include "fs/state.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Hard Link Test

char const target_path1[] = "/f1";
char const link_path1[] = "/l1";

int check_hard_link_counter(char const *name) {
    int inum = tfs_lookup(name);

    inode_t *inode = inode_get(inum);

    if (inode->i_hardlink_counter > 1)
        return 0;
    if (inode->i_hardlink_counter == 1)
        return 1;
    return -1;
}

int main() {
    assert(tfs_init(NULL) != -1);

    int f = tfs_open(target_path1, TFS_O_CREAT);
    assert(f != -1);
    assert(tfs_close(f) != -1);

    // does a hard link with the target file
    assert(tfs_link(target_path1, link_path1) == 0);

    // lets check if the hard link has been created like it was supposed to
    int i = tfs_open(link_path1, 0);
    assert(i != -1);
    assert(tfs_close(i) != -1);

    // checks if the hardlink counter asscociated with the target has been
    // incremented or not
    assert(check_hard_link_counter(target_path1) == 0);

    // eliminate the hard link that was just created before
    assert(tfs_unlink(link_path1) == 0);

    // checks if the hardlink counter asscociated with the target has been
    // decremented or not
    assert(check_hard_link_counter(target_path1) == 1);

    // ends the test
    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}