#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "operations.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

/**
 * Directory entry
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY, T_SYMLINK } inode_type;
// adicionado um tipo de inode para symlink

/**
 * Inode
 */
typedef struct {
    inode_type i_node_type;

    size_t i_size;
    int i_data_block;
    int i_hardlink_counter;
    char i_symlink_target[MAX_FILE_NAME];
    // in a more complete FS, more fields could exist here
} inode_t;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t;

/**
 * Open file entry (in open file table)
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
} open_file_entry_t;

pthread_rwlock_t *get_inode_table_lock(int inumber);
pthread_rwlock_t *get_open_file_table_lock(int file_handle);

void lock_mutex(pthread_mutex_t *mutex);
void read_lock_rwlock(pthread_rwlock_t *rwlock);
void write_lock_rwlock(pthread_rwlock_t *rwlock);
void unlock_mutex(pthread_mutex_t *mutex);
void unlock_rwlock(pthread_rwlock_t *rwlock);
void init_mutex(pthread_mutex_t *mutex);
void init_rwlock(pthread_rwlock_t *rwlock);
void destroy_mutex(pthread_mutex_t *mutex);
void destroy_rwlock(pthread_rwlock_t *rwlock);

int state_init(tfs_params);
int state_destroy(void);

size_t state_block_size(void);

int inode_create(inode_type n_type);
void inode_delete(int inumber);
inode_t *inode_get(int inumber);

int clear_dir_entry(inode_t *inode, char const *sub_name);
int add_dir_entry(inode_t *inode, char const *sub_name, int sub_inumber);
int find_in_dir(inode_t const *inode, char const *sub_name);

int data_block_alloc(void);
void data_block_free(int block_number);
void *data_block_get(int block_number);

int add_to_open_file_table(int inumber, size_t offset);
void remove_from_open_file_table(int fhandle);
open_file_entry_t *get_open_file_entry(int fhandle);

/* Stores the number of currently open files */
extern int open_files_count;

/* Condition variable and mutex */
extern pthread_cond_t open_files_cond;
extern pthread_mutex_t open_files_mutex;
extern pthread_mutex_t open_file_lock;

#endif // STATE_H
