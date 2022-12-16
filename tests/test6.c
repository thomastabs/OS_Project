#include "../fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(){

    // tfs_copy_from_external_fs and copying multiple times for the same file

    char *path_copied_file1 = "/f1";
    char *path_src1 = "tests/file_to_copy3.txt"; 
    char *path_src2 = "tests/file_to_copy4.txt";
    char *path_src3 = "tests/file_to_copy5.txt";
    char *path_src4 = "tests/file_to_copy6.txt";
    char buffer[30];

    char *str_ext_file1 = "Bom dia";
    char *str_ext_file2 = "Boa noite";
    char *total_ext_file = "Bom dia Boa Tarde Boa noite";
    assert(tfs_init(NULL) != -1);

    int f;
    ssize_t r;
    size_t i;

    // in this test, we will copy essencially 3 files in top of each other
    // and check if something has been lost, and checking in the
    // end if the result is equal to the last thing that was copied,
    // overwriting the other copies

    // copy "Bom dia"
    f = tfs_copy_from_external_fs(path_src1, path_copied_file1); 
    assert(f != -1);

    f = tfs_open(path_copied_file1, TFS_O_CREAT);
    assert(f != -1);

    // comparing both buffer and str_ext_file1, 
    // first copy try to check if is everything alright
    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    i = strlen(str_ext_file1);
    assert(r == i);
    assert(!memcmp(buffer, str_ext_file1, strlen(str_ext_file1)));

    // copy "Boa tarde"
    f = tfs_copy_from_external_fs(path_src2, path_copied_file1);
    assert(f != -1);

    // copy "Boa noite"
    f = tfs_copy_from_external_fs(path_src3, path_copied_file1);
    assert(f != -1);

    f = tfs_open(path_copied_file1, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    i = strlen(str_ext_file2);
    assert(r == i);
    assert(!memcmp(buffer, str_ext_file2, strlen(str_ext_file2)));
    
    // after checking that the last result is in fact "Boa noite" 
    // by overwriting the previous copies, lets make a final copy
    
    // copy "Bom dia Boa Tarde Boa noite"
    f = tfs_copy_from_external_fs(path_src4, path_copied_file1);
    assert(f != -1);

    f = tfs_open(path_copied_file1, TFS_O_CREAT);
    assert(f != -1);

    // Lets make the final check for the "Bom dia Boa Tarde Boa noite" copy
    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    i = strlen(total_ext_file);
    assert(r == i);
    assert(!memcmp(buffer, total_ext_file, strlen(total_ext_file)));
    
    printf("Successful test.\n");

    return 0;
}