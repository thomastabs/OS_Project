#include "fs/operations.h"
#include "fs/state.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Sym Link Test, target path check, 
// hard link with soft link test and vice versa

uint8_t const file_contents[] = "AAA!";
char const target_path1[] = "/f1";
char const link_path1[] = "/l1";
char const target_path2[] = "/f2";
char const link_path2[] = "/l2";

void assert_contents_ok(char const *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(memcmp(buffer, file_contents, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

void write_contents(char const *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    assert(tfs_write(f, file_contents, sizeof(file_contents)) ==
           sizeof(file_contents));

    assert(tfs_close(f) != -1);
}

int check_target_path_in_symLink(char const *link_path, char const *target_path){
    int link_path_inum = tfs_lookup(link_path);
    if (link_path_inum == -1)
        return -1; 

    int target_inum = tfs_lookup(target_path);
    if (target_inum == -1)
        return -1; 

    inode_t *link_path_inode = inode_get(link_path_inum);
    if (link_path_inode == NULL)
        return -1;

    if (strcmp(link_path_inode->i_symlink_target, target_path) == 0)
        return 0;
    return -1;
}

int main(){
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

    // after checking if the symbolic link was created, 
    // lets check if the path of the target was saved sucessfully on the 
    // SymLink file that was created 
    assert(check_target_path_in_symLink(link_path1, target_path1) == 0);

    // now that the target has been sucessfully saved, lets try to 
    // write on the target and access it via the symbolic link path

    write_contents(target_path1);
    assert_contents_ok(target_path1);
    assert_contents_ok(link_path1);

    // now lets make a hard link with a soft link - which should not work at all
    assert(tfs_link(link_path1, link_path2) == -1);

    // and a soft link with a hard link? well it should work because
    // the symbolic still would be acessing the target's inode, through
    // the hard link's inode which is the target's
   
    assert(tfs_link(target_path1, link_path2) == 0);   

    assert(tfs_sym_link(link_path1, link_path2) == 0);

    assert(tfs_destroy() != -1);
    printf("Successful test.\n");

    return 0;
}