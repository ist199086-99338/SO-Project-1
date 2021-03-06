#include "state.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Persistent FS state  (in reality, it should be maintained in secondary
 * memory; for simplicity, this project maintains it in primary memory) */

/* I-node table */
static inode_t inode_table[INODE_TABLE_SIZE];
static char freeinode_ts[INODE_TABLE_SIZE];

/* Data blocks */
static char fs_data[BLOCK_SIZE * DATA_BLOCKS];
static char free_blocks[DATA_BLOCKS];

/* file_entries Lock */
static pthread_mutex_t free_open_file_entries_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t freeinode_ts_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t free_blocks_lock = PTHREAD_MUTEX_INITIALIZER;

/* Volatile FS state */

static open_file_entry_t open_file_table[MAX_OPEN_FILES];
static char free_open_file_entries[MAX_OPEN_FILES];

static inline bool valid_inumber(int inumber) {
    return inumber >= 0 && inumber < INODE_TABLE_SIZE;
}

static inline bool valid_block_number(int block_number) {
    return block_number >= 0 && block_number < DATA_BLOCKS;
}

static inline bool valid_file_handle(int file_handle) {
    return file_handle >= 0 && file_handle < MAX_OPEN_FILES;
}

/**
 * We need to defeat the optimizer for the insert_delay() function.
 * Under optimization, the empty loop would be completely optimized away.
 * This function tells the compiler that the assembly code being run (which is
 * none) might potentially change *all memory in the process*.
 *
 * This prevents the optimizer from optimizing this code away, because it does
 * not know what it does and it may have side effects.
 *
 * Reference with more information: https://youtu.be/nXaxk27zwlk?t=2775
 *
 * Exercise: try removing this function and look at the assembly generated to
 * compare.
 */
static void touch_all_memory() { __asm volatile("" : : : "memory"); }

/*
 * Auxiliary function to insert a delay.
 * Used in accesses to persistent FS state as a way of emulating access
 * latencies as if such data structures were really stored in secondary memory.
 */
static void insert_delay() {
    for (int i = 0; i < DELAY; i++) {
        touch_all_memory();
    }
}

/*
 * Initializes FS state
 */
void state_init() {
    for (size_t i = 0; i < INODE_TABLE_SIZE; i++) {
        freeinode_ts[i] = FREE;
    }

    for (size_t i = 0; i < DATA_BLOCKS; i++) {
        free_blocks[i] = FREE;
    }

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        free_open_file_entries[i] = FREE;
        init_mlock(&open_file_table[i].of_lock);
    }
}

void state_destroy() { /* nothing to do */
    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        destroy_mlock(&open_file_table[i].of_lock);
    }

    destroy_mlock(&free_open_file_entries_lock);
    destroy_mlock(&freeinode_ts_lock);
}

/*
 * Creates a new i-node in the i-node table.
 * Input:
 *  - n_type: the type of the node (file or directory)
 * Returns:
 *  new i-node's number if successfully created, -1 otherwise
 */
// TODO: add mutex
int inode_create(inode_type n_type) {
    // lock access to freeinode_ts
    mutex_lock(&freeinode_ts_lock);

    for (int inumber = 0; inumber < INODE_TABLE_SIZE; inumber++) {
        if ((inumber * (int)sizeof(allocation_state_t) % BLOCK_SIZE) == 0) {
            insert_delay(); // simulate storage access delay (to freeinode_ts)
        }

        /* Finds first free entry in i-node table */
        if (freeinode_ts[inumber] == FREE) {
            /* Found a free entry, so takes it for the new i-node*/
            freeinode_ts[inumber] = TAKEN;
            mutex_unlock(&freeinode_ts_lock);

            insert_delay(); // simulate storage access delay (to i-node)
            inode_table[inumber].i_node_type = n_type;

            if (n_type == T_DIRECTORY) {
                /* Initializes directory (filling its block with empty
                 * entries, labeled with inumber==-1) */
                int b = data_block_alloc();
                if (b == -1) {
                    freeinode_ts[inumber] = FREE;
                    return -1;
                }

                inode_table[inumber].i_size = BLOCK_SIZE;
                inode_table[inumber].i_data_direct_blocks[0] = b;

                dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(b);
                if (dir_entry == NULL) {
                    freeinode_ts[inumber] = FREE;
                    return -1;
                }

                for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
                    dir_entry[i].d_inumber = -1;
                }
            } else {
                /* In case of a new file, simply sets its size to 0 */
                inode_table[inumber].i_size = 0;
                for (size_t i = 0; i < 10; i++) {
                    inode_table[inumber].i_data_direct_blocks[i] = -1;
                }
                inode_table[inumber].i_data_indirect_block = -1;
            }

            init_rwlock(&inode_table[inumber].i_lock);
            return inumber;
        }
    }
        
    mutex_unlock(&freeinode_ts_lock);
    return -1;
}

/*
 * Deletes the i-node.
 * Input:
 *  - inumber: i-node's number
 * Returns: 0 if successful, -1 if failed
 */
int inode_delete(int inumber) {
    // simulate storage access delay (to i-node and freeinode_ts)
    insert_delay();
    insert_delay();

    if (!valid_inumber(inumber) || freeinode_ts[inumber] == FREE) {
        return -1;
    }

    freeinode_ts[inumber] = FREE;

    write_lock(&inode_table[inumber].i_lock);

    int r = iterate_blocks(&inode_table[inumber], 0,
                           (int)(inode_table[inumber].i_size / BLOCK_SIZE) + 1,
                           &data_block_free);

    rw_unlock(&inode_table[inumber].i_lock);

    destroy_rwlock(&inode_table[inumber].i_lock);

    return r;
}

/*
 * Returns a pointer to an existing i-node.
 * Input:
 *  - inumber: identifier of the i-node
 * Returns: pointer if successful, NULL if failed
 */
inode_t *inode_get(int inumber) {
    if (!valid_inumber(inumber)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to i-node
    return &inode_table[inumber];
}

/*
 * Adds an entry to the i-node directory data.
 * Input:
 *  - inumber: identifier of the i-node
 *  - sub_inumber: identifier of the sub i-node entry
 *  - sub_name: name of the sub i-node entry
 * Returns: SUCCESS or FAIL
 */
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name) {
    if (!valid_inumber(inumber) || !valid_inumber(sub_inumber)) {
        return -1;
    }

    insert_delay(); // simulate storage access delay to i-node with inumber
    if (inode_table[inumber].i_node_type != T_DIRECTORY) {
        return -1;
    }

    if (strlen(sub_name) == 0) {
        return -1;
    }

    /* Locates the block containing the directory's entries */
    dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(
        inode_table[inumber].i_data_direct_blocks[0]);
    if (dir_entry == NULL) {
        return -1;
    }

    /* Finds and fills the first empty entry */
    for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (dir_entry[i].d_inumber == -1) {
            dir_entry[i].d_inumber = sub_inumber;
            strncpy(dir_entry[i].d_name, sub_name, MAX_FILE_NAME - 1);
            dir_entry[i].d_name[MAX_FILE_NAME - 1] = 0;
            return 0;
        }
    }

    return -1;
}

/* Looks for a given name inside a directory
 * Input:
 * 	- parent directory's i-node number
 * 	- name to search
 * 	Returns i-number linked to the target name, -1 if not found
 */
int find_in_dir(int inumber, char const *sub_name) {
    insert_delay(); // simulate storage access delay to i-node with inumber
    if (!valid_inumber(inumber) ||
        inode_table[inumber].i_node_type != T_DIRECTORY) {
        return -1;
    }

    /* Locates the block containing the directory's entries */
    dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(
        inode_table[inumber].i_data_direct_blocks[0]);
    if (dir_entry == NULL) {
        return -1;
    }

    /* Iterates over the directory entries looking for one that has the target
     * name */
    for (int i = 0; i < MAX_DIR_ENTRIES; i++)
        if ((dir_entry[i].d_inumber != -1) &&
            (strncmp(dir_entry[i].d_name, sub_name, MAX_FILE_NAME) == 0)) {
            return dir_entry[i].d_inumber;
        }

    return -1;
}

/*
 * Allocated a new data block
 * Returns: block index if successful, -1 otherwise
 */
int data_block_alloc() {
    mutex_lock(&free_blocks_lock);

    for (int i = 0; i < DATA_BLOCKS; i++) {
        if (i * (int)sizeof(allocation_state_t) % BLOCK_SIZE == 0) {
            insert_delay(); // simulate storage access delay to free_blocks
        }

        if (free_blocks[i] == FREE) {
            free_blocks[i] = TAKEN;
            mutex_unlock(&free_blocks_lock); 
            return i;
        }
    }

    mutex_unlock(&free_blocks_lock);
    return -1;
}

/* Frees a data block
 * Input
 * 	- the block index
 * Returns: 0 if success, -1 otherwise
 */
int data_block_free(int *block_number) {
    if (!valid_block_number(*block_number)) {
        return -1;
    }

    insert_delay(); // simulate storage access delay to free_blocks
    
    mutex_lock(&free_blocks_lock);
    free_blocks[*block_number] = FREE;
    mutex_unlock(&free_blocks_lock);
    
    return 0;
}

/* Returns a pointer to the contents of a given block
 * Input:
 * 	- Block's index
 * Returns: pointer to the first byte of the block, NULL otherwise
 */
void *data_block_get(int block_number) {
    if (!valid_block_number(block_number)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to block
    return &fs_data[block_number * BLOCK_SIZE];
}

/* Add new entry to the open file table
 * Inputs:
 * 	- I-node number of the file to open
 * 	- Initial offset
 * Returns: file handle if successful, -1 otherwise
 */
int add_to_open_file_table(int inumber, size_t offset) {
    inode_t *inode = inode_get(inumber);

    if (inode == NULL) {
        return -1;
    }

    // Lock it so that no 2 threads can take the same open_file_entry
    mutex_lock(&free_open_file_entries_lock);

    // TODO: Check lock type (Fixed to compile)
    write_lock(&inode->i_lock);

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (free_open_file_entries[i] == FREE) {
            free_open_file_entries[i] = TAKEN;

            // As soon as the file entry changes to TAKEN the lock can be freed
            mutex_unlock(&free_open_file_entries_lock);

            open_file_table[i].of_inumber = inumber;
            open_file_table[i].of_offset = offset;
            
            rw_unlock(&inode->i_lock);
            return i;
        }
    }

    mutex_unlock(&free_open_file_entries_lock);

    rw_unlock(&inode->i_lock);
    return -1;
}

/* Frees an entry from the open file table
 * Inputs:
 * 	- file handle to free/close
 * Returns 0 is success, -1 otherwise
 */
int remove_from_open_file_table(int fhandle) {
    mutex_lock(&free_open_file_entries_lock);

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    write_lock(&inode->i_lock);

    if (!valid_file_handle(fhandle) ||
        free_open_file_entries[fhandle] != TAKEN) {
        rw_unlock(&inode->i_lock);
        return -1;
    }
    free_open_file_entries[fhandle] = FREE;

    mutex_unlock(&free_open_file_entries_lock);

    mutex_unlock(&file->of_lock);
    rw_unlock(&inode->i_lock);

    return 0;
}

/* Returns pointer to a given entry in the open file table
 * Inputs:
 * 	 - file handle
 * Returns: pointer to the entry if sucessful, NULL otherwise
 */
open_file_entry_t *get_open_file_entry(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return NULL;
    }
    open_file_entry_t *file = &open_file_table[fhandle];

    if(file == NULL)
        return NULL;
    
    mutex_lock(&file->of_lock);

    return &open_file_table[fhandle];
}

/*
 * Assigns an index to a given block index pointer
 * Inputs:
 *   - block: pointer to the index of the block to allocate
 * Returns: 0 if successful, -1 otherwise
 */
int allocate_block_aux(int *block) {
    *block = data_block_alloc();

    // Safety check for lack of memory
    if (*block == -1)
        return -1;

    return 0;
}

/*
 *  Iterates data blocks, applying the effect of a given function
 *  Inputs:
 *      - inode: inode to iterate the blocks of
 *      - current: index of the block to start iteration
 *      - end: index of the block to end iteration
 *      - f: function to call on each loop, it accepts the iteration block
 *           pointer as argument
 *  Returns:
 *      (int) 0 OK, -1 error
 */
int iterate_blocks(inode_t *inode, int current, int end,
                   int (*foo)(int *block)) {
    if (current > end)
        return -1;

    // The first block to access is on the first 10 blocks,
    // which can be accessed directly
    while (current < 10 && current < end) {
        if (foo(&inode->i_data_direct_blocks[current++]) == -1) {
            return -1;
        }
    }

    // iterate throw direct block on indirect block
    // Get indirect block data to access direct block
    if (end < 10)
        return 0;

    if (inode->i_data_indirect_block == -1)
        if ((inode->i_data_indirect_block = data_block_alloc()) == -1)
            return -1;
    int *direct_block = (int *)data_block_get(inode->i_data_indirect_block);
    if (direct_block == NULL)
        return -1;
    while (current < end) {
        // maybe check if the block isn't free?
        if (foo(direct_block) == -1)
            return -1; // Oh no, something went wrong.
        direct_block += sizeof(int);
        current++;
    }

    return 0;
}

/*
    Writes data on block
    Inputs:
        - of_offset: Offset of the file handle
        - block_offset: Where in the block to start writting
        - to_write: Amount of data to write in block
        - block: Block to write on
        - buffer: Input buffer
        - buffer_offset: Offset in the buffer to start reading from
        - inode: inode
    Output:
        0: Everything went fine
        -1: Error
*/
int write_to_block(size_t *of_offset, int block_offset, size_t *to_write,
                   void *block, void const *buffer, size_t buffer_offset,
                   inode_t *inode) {
    /* Perform the actual write */
    // size_t to_write_in_block =
    //     (size_t)((int)to_write % BLOCK_SIZE - initial_offset);

    size_t to_write_in_block = (*to_write > BLOCK_SIZE)
                                   ? (size_t)(BLOCK_SIZE - block_offset)
                                   : (size_t)((int)*to_write - block_offset);

    *to_write -= to_write_in_block;
    memcpy(block + block_offset, buffer + buffer_offset, to_write_in_block);

    /* The offset associated with the file handle is
     * incremented accordingly */
    *of_offset += to_write_in_block;
    if ((size_t)*of_offset > inode->i_size) {
        inode->i_size = (size_t)*of_offset;
    }

    return 0;
}

/* TODO: fix comment
    Reads data from block
    Inputs:
        - of_offset: Where in the block to start reading from
        - to_read: Amount of data to read from block
        - block: Block to write on
        - buffer: Output buffer
        - buffer_offset: Offset in the buffer to start writting on
    Output:
        0: Everything went fine
        -1: Error
*/
int read_from_block(int offset, size_t *to_read, void *block, void *buffer,
                    size_t buffer_offset) {
    /* Perform the actual read */
    size_t to_read_from_block = (*to_read > BLOCK_SIZE)
                                    ? (size_t)(BLOCK_SIZE - offset)
                                    : (size_t)((int)*to_read - offset);

    *to_read -= to_read_from_block;

    memcpy(buffer + buffer_offset, block + offset, to_read_from_block);

    return 0;
}