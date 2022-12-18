#include "operations.h"
#include "config.h"
#include "state.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betterassert.h"

tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

int tfs_init(tfs_params const *params_ptr) {
    tfs_params params;
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
int tfs_lookup(char const *name) {
    inode_t *root_inode = inode_get(ROOT_DIR_INUM);

    if (!valid_pathname(name)) {
        return -1;
    }
    // skip the initial '/' character
    name++;

    return find_in_dir(root_inode, name);
}

int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open: root dir inode must exist");
    int inum = tfs_lookup(name);
    size_t offset;

    if (inum >= 0) {
        // The file already exists
        inode_t *inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");

        // if the target is a SymLink type of file,
        // then instead of opening of the symlink file
        // itself, the symlink target path is the one
        // opened by overwriting the values already made
        if (inode->i_node_type == T_SYMLINK) {
            inum = tfs_lookup(inode->i_symlink_target);
            if (inum == -1)
                return -1;

            inode = inode_get(inum);
            if (inode == NULL)
                return -1;
        }

        // Truncate (if requested)
        if (mode & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);
                write_lock_rwlock(inode_lock);

                data_block_free(inode->i_data_block);
                inode->i_size = 0;

                unlock_rwlock(inode_lock);
            }
        }
        // Determine initial offset
        if (mode & TFS_O_APPEND) {
            pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);
            write_lock_rwlock(inode_lock);

            offset = inode->i_size;

            if ((inode->i_node_type == T_SYMLINK) &&
                (tfs_lookup(inode->i_symlink_target) == -1)) {
                unlock_rwlock(inode_lock);
                return -1;
            }
            unlock_rwlock(inode_lock);

        } else {
            offset = 0;
        }
    } else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1; // no space in inode table
        }

        inode_t *name_inode = inode_get(inum);
        if ((name_inode->i_node_type == T_SYMLINK) &&
            (tfs_lookup(name_inode->i_symlink_target) == -1) &&
            (mode != TFS_O_CREAT)) {
            return -1;
        }

        pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);
        write_lock_rwlock(inode_lock);

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            unlock_rwlock(inode_lock);
            return -1; // no space in directory
        }

        offset = 0;
        unlock_rwlock(inode_lock);
    } else {
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

int tfs_sym_link(char const *target, char const *link_name) {
    inode_t *root = inode_get(ROOT_DIR_INUM); // 0 - root inumber
    int target_inumber = tfs_lookup(target);
    if (root == NULL || target_inumber == -1) {
        return -1;
    }

    int symlink_inumber = inode_create(T_SYMLINK);
    if (symlink_inumber == -1) {
        return -1;
    }

    inode_t *symlink_inode = inode_get(symlink_inumber);
    if (symlink_inode == NULL) {
        return -1;
    }

    strncpy(symlink_inode->i_symlink_target, target, MAX_FILE_NAME - 1);

    int link = add_dir_entry(root, link_name + 1, symlink_inumber);
    if (link == -1) {
        return -1;
    }
    return 0;
}

int tfs_link(char const *target_file, char const *link_path) {
    inode_t *root = inode_get(ROOT_DIR_INUM); // 0 - root inumber
    if (root == NULL) {
        return -1;
    }

    int target_inumber = tfs_lookup(target_file);
    if (target_inumber == -1)
        return -1;

    pthread_rwlock_t *inode_lock = get_inode_table_lock(target_inumber);
    write_lock_rwlock(inode_lock);

    inode_t *target_inode = inode_get(target_inumber);
    if (target_inode == NULL) {
        unlock_rwlock(inode_lock);
        return -1;
    }

    if (target_inode->i_node_type & T_SYMLINK) {
        // bloqueia a tentativa de fzr hard link com um target Symbolic Link
        unlock_rwlock(inode_lock);
        return -1;
    }

    int link = add_dir_entry(root, link_path + 1, target_inumber);
    if (link == -1) {
        unlock_rwlock(inode_lock);
        return -1;
    }
    target_inode->i_hardlink_counter++;
    unlock_rwlock(inode_lock);
    return 0;
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1; // invalid fd
    }

    remove_from_open_file_table(fhandle);

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    pthread_rwlock_t *file_lock = get_open_file_table_lock(fhandle);

    //  From the open file table entry, we get the inode
    write_lock_rwlock(file_lock);
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");

    pthread_rwlock_t *inode_lock = get_inode_table_lock(file->of_inumber);
    write_lock_rwlock(inode_lock);

    // Determine how many bytes to write
    size_t block_size = state_block_size();
    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                unlock_rwlock(file_lock);
                unlock_rwlock(inode_lock);
                return -1; // no space
            }

            inode->i_data_block = bnum;
        }

        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        // Perform the actual write
        memcpy(block + file->of_offset, buffer, to_write);

        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    }
    unlock_rwlock(file_lock);
    unlock_rwlock(inode_lock);

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }
    pthread_rwlock_t *file_lock = get_open_file_table_lock(fhandle);

    // From the open file table entry, we get the inode
    write_lock_rwlock(file_lock);
    inode_t const *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

    pthread_rwlock_t *inode_lock = get_inode_table_lock(file->of_inumber);

    // Determine how many bytes to read
    write_lock_rwlock(inode_lock);
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + file->of_offset, to_read);
        // The offs´et associated with the file handle is incremented
        // accordingly
        file->of_offset += to_read;
    }

    unlock_rwlock(file_lock);
    unlock_rwlock(inode_lock);
    return (ssize_t)to_read;
}

int tfs_unlink(char const *target) {
    inode_t *root = inode_get(ROOT_DIR_INUM); // 0 - root inumber
    int target_inumber = tfs_lookup(target);  // ve se o target existe
    if (root == NULL || target_inumber == -1) {
        return -1;
    }
    pthread_rwlock_t *inode_lock = get_inode_table_lock(target_inumber);
    write_lock_rwlock(inode_lock);

    inode_t *target_inode = inode_get(target_inumber);
    if (target_inode == NULL) {
        unlock_rwlock(inode_lock);
        return -1;
    }
    if (target_inode->i_hardlink_counter >= 1 &&
        !(target_inode->i_node_type & T_SYMLINK)) {
        // aqui o target ou é um path de um hard link ou um fich normal
        target_inode->i_hardlink_counter--;
        if (target_inode->i_hardlink_counter == 0) {
            // apagar o unico hard link q tem com o proprio inode
            clear_dir_entry(root, target + 1);
            inode_delete(target_inumber);
            unlock_rwlock(inode_lock);
            return 0;
        }
        clear_dir_entry(root, target + 1);
        unlock_rwlock(inode_lock);
        return 0;
    } else if (target_inode->i_node_type & T_SYMLINK) {
        clear_dir_entry(root, target + 1);
        inode_delete(target_inumber);
        unlock_rwlock(inode_lock);
        return -1;
    } else {
        unlock_rwlock(inode_lock);
        return -1;
    }
}

int tfs_copy_from_external_fs(char const *source_path, char const *dest_path) {
    FILE *source_file = fopen(source_path, "r");
    if (source_file == NULL) {
        return -1;
    }

    int dest_handle = tfs_open(dest_path, TFS_O_CREAT | TFS_O_TRUNC);
    if (dest_handle == -1) {
        return -1;
    }

    open_file_entry_t *dest_file = get_open_file_entry(dest_handle);
    if (dest_file == NULL) {
        return -1;
    }

    int dest_inum = dest_file->of_inumber;
    inode_t *dest_inode = inode_get(dest_inum);
    if (dest_inode == NULL) {
        return -1;
    }

    size_t bytes_read;
    void *buffer;
    buffer = malloc(tfs_default_params().block_size);
    if (buffer == NULL) {
        return -1;
    }

    do {
        memset(buffer, 0, tfs_default_params().block_size);
        bytes_read =
            fread(buffer, 1, tfs_default_params().block_size, source_file);
        if (bytes_read == -1) {
            free(buffer);
            return -1;
        }
        size_t bytes_to_be_written = bytes_read;
        ssize_t bytes_written = tfs_write(dest_handle, buffer, bytes_read);
        if (bytes_written != bytes_to_be_written) {
            free(buffer);
            return -1;
        }
    } while (bytes_read ==
             tfs_default_params()
                 .block_size); // stops when it reads less than BLOCK_SIZE bytes

    free(buffer);

    fclose(source_file);
    if (tfs_close(dest_handle) == -1) {
        return -1;
    }
    return 0;
}
