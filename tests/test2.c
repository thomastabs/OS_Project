#include "../fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {

    char *str_ext_file = 
        "As armas e os Baroes assinalados, que da Ocidental praia Lusitana, "
        "por mares nunca de antes navegados. Passaram ainda alem da Taprobana, "
        "em perigos e guerras esforcados mais do que prometia a forca humana, "
        "e entre gente remota edificaram novo Reino, que tanto sublimaram; "
        "e também as memorias gloriosas daqueles Reis que foram dilatando. "
        "A Fe, o Imperio, e as terras viciosas de Africa e de Asia andaram devastando, "
        "e aqueles que por obras valerosas se vao da lei da Morte libertando, "
        "cantando espalharei por toda parte, se a tanto me ajudar o engenho e arte.";
    char *path_copied_file = "/f2";
    char *path_src = "tests/file_to_copy2.txt";
    char buffer[600];

    assert(tfs_init(NULL) != -1);

    int f;
    ssize_t r;

    f = tfs_copy_from_external_fs(path_src, path_copied_file);
    assert(f != -1);

    f = tfs_open(path_copied_file, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str_ext_file));
    assert(!memcmp(buffer, str_ext_file, strlen(str_ext_file)));

    printf("Successful test.\n");

    return 0;
}
