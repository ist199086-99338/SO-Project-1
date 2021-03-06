#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

/*
 * Directory entry
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY } inode_type;

/*
 * I-node
 */
typedef struct {
    inode_type i_node_type;
    size_t i_size;
    int i_data_direct_blocks[10];
    int i_data_indirect_block;
    pthread_rwlock_t i_lock;
    /* in a real FS, more fields would exist here */
} inode_t;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t;

/*
 * Open file entry (in open file table)
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
    pthread_mutex_t of_lock;
} open_file_entry_t;

#define MAX_DIR_ENTRIES (BLOCK_SIZE / sizeof(dir_entry_t))

void state_init();
void state_destroy();

int inode_create(inode_type n_type);
int inode_delete(int inumber);
inode_t *inode_get(int inumber);

int free_block_aux(int *block);
int allocate_block_aux(int *block);
int iterate_blocks(inode_t *inode, int start, int end, int (*f)(int *block));

int clear_dir_entry(int inumber, int sub_inumber);
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name);
int find_in_dir(int inumber, char const *sub_name);

int data_block_alloc();
int data_block_free(int *block_number);
void *data_block_get(int block_number);

int read_from_block(int offset, size_t *to_read, void *block, void *buffer,
                    size_t buffer_offset);
int write_to_block(size_t *of_offset, int block_offset, size_t *to_write,
                   void *block, void const *buffer, size_t buffer_offset,
                   inode_t *inode);

int add_to_open_file_table(int inumber, size_t offset);
int remove_from_open_file_table(int fhandle);
open_file_entry_t *get_open_file_entry(int fhandle);

#endif // STATE_H
