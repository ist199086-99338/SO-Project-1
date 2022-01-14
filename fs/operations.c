#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);

        if (inode == NULL) {
            return -1;
        }

        // TODO: Fixed in order to compile, check lock type
        write_lock(&inode->i_lock);

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                if (iterate_blocks(inode, 0,
                                   (int)(inode->i_size / BLOCK_SIZE) + 1,
                                   &data_block_free) == -1) {
                    rw_unlock(&inode->i_lock);
                    return -1;
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }

        rw_unlock(&inode->i_lock);

    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be
         * created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else
        return -1;

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and
     * there is an error adding an entry to the open file table, the file is
     * not opened but it remains created */
}

int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

/*
    Aborts an operation, closing the tfs file and returning -1
*/
int abort_operation(int fhandle) {
    tfs_close(fhandle);
    return -1;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        mutex_unlock(&file->of_lock);
        return -1;
    }

    size_t to_write_remaining = to_write;
    if (to_write > 0) {
        write_lock(&inode->i_lock);

        if (inode->i_size == 0) {
            /* If empty file, allocate new blocks */
            if (iterate_blocks(inode, 0, (int)(to_write / BLOCK_SIZE) + 1,
                               &allocate_block_aux) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                
                return -1;
            }
        }

        int current = ((int)file->of_offset / BLOCK_SIZE);
        int end = current + ((int)to_write / BLOCK_SIZE) + 1;
        int initial_offset = (int)file->of_offset % BLOCK_SIZE;

        while (current < 10 && current < end) {
            if (inode->i_data_direct_blocks[current] == -1)
                if ((inode->i_data_direct_blocks[current] =
                         data_block_alloc()) == -1) {
                    rw_unlock(&inode->i_lock);
                    mutex_unlock(&file->of_lock);

                    return -1;
                }

            void *block = data_block_get(inode->i_data_direct_blocks[current]);
            if (block == NULL) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);

                return -1;
            }

            if (write_to_block(&file->of_offset, initial_offset,
                               &to_write_remaining, block, buffer,
                               to_write - to_write_remaining, inode) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                
                return -1;
            }

            initial_offset = 0;
            current++;
        }

        // iterate throw direct block on indirect block
        // Get indirect block data to access direct block

        // Check if the inode has an indirect block allocated
        if (inode->i_data_indirect_block == -1)
            if ((inode->i_data_indirect_block = data_block_alloc()) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);

                return -1;
            }

        int *indirect_block =
            (int *)data_block_get(inode->i_data_indirect_block);
        if (indirect_block == NULL) {
            rw_unlock(&inode->i_lock);
            mutex_unlock(&file->of_lock);
            return -1;
        }
        while (current < end) {
            int indirection_block_index = *(((int *)indirect_block));

            void *block = data_block_get(indirection_block_index);
            if (block == NULL) {
                rw_unlock(&inode->i_lock);mutex_unlock(&file->of_lock);
                mutex_unlock(&file->of_lock);
                return -1;
            }

            write_lock(&inode->i_lock);
            if (write_to_block(&file->of_offset, initial_offset,
                               &to_write_remaining, block, buffer,
                               to_write - to_write_remaining, inode) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                return -1; // Hol up, wait a minute, something aint right.
            }

            indirect_block += sizeof(int);

            initial_offset = 0;
            current++;
        }
        mutex_unlock(&file->of_lock);
        rw_unlock(&inode->i_lock);
    }

    // Increment i_size only of to_write plus offset is bigger then i_size
    // if (to_write > (inode->i_size - inittial_offset))
    //    inode->i_size = to_write - (inode->i_size - inittial_offset);

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        mutex_unlock(&file->of_lock);
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        mutex_unlock(&file->of_lock);
        return -1;
    }
    read_lock(&inode->i_lock);

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    size_t to_read_remaining = to_read;
    if (to_read > 0) {

        int current = ((int)file->of_offset / BLOCK_SIZE);
        int end = current + ((int)to_read / BLOCK_SIZE) + 1;
        int initial_offset = (int)file->of_offset % BLOCK_SIZE;

        while (current < 10 && current < end) {
            if (inode->i_data_direct_blocks[current] == -1) {
                if ((inode->i_data_direct_blocks[current] =
                         data_block_alloc()) == -1) {
                    rw_unlock(&inode->i_lock);
                    mutex_unlock(&file->of_lock);
                    return -1;
                }
            }

            void *block = data_block_get(inode->i_data_direct_blocks[current]);
            if (block == NULL) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                return -1;
            }

            if (read_from_block(initial_offset, &to_read_remaining, block,
                                buffer, to_read - to_read_remaining) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                return -1;
            }

            initial_offset = 0;
            current++;
        }

        // iterate throw direct block on indirect block
        // Get indirect block data to access direct block

        // Check if the inode has an indirect block allocated
        if (inode->i_data_indirect_block == -1)
            if ((inode->i_data_indirect_block = data_block_alloc()) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                return -1;
            }

        int *indirect_block =
            (int *)data_block_get(inode->i_data_indirect_block);
        if (indirect_block == NULL) {
            rw_unlock(&inode->i_lock);
            mutex_unlock(&file->of_lock);
            return -1;
        }

        while (current < end) {
            int indirection_block_index = *(((int *)indirect_block));

            void *block = data_block_get(indirection_block_index);
            if (block == NULL) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                return -1;
            }

            read_lock(&inode->i_lock);
            if (read_from_block(initial_offset, &to_read_remaining, block,
                                buffer, to_read - to_read_remaining) == -1) {
                rw_unlock(&inode->i_lock);
                mutex_unlock(&file->of_lock);
                return -1; // Hol up, wait a minute, something aint right.
            }

            indirect_block += sizeof(int);

            initial_offset = 0;
            current++;
        }
    }

    mutex_unlock(&file->of_lock);
    rw_unlock(&inode->i_lock);

    return (ssize_t)to_read;
}

int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    // Check if source file exists
    int inumber;
    int fhandle;

    // Open the file for reading
    if ((fhandle = tfs_open(source_path, TFS_O_START)) == -1)
        return -1;

    if ((inumber = tfs_lookup(source_path)) == -1)
        return abort_operation(fhandle);

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(inumber);
    if (inode == NULL) {
        return abort_operation(fhandle);
    }

    FILE *dest_file = fopen(dest_path, "w");
    if (dest_file == NULL)
        return abort_operation(fhandle);

    // Write in dest_file
    char *buffer;
    buffer = malloc(inode->i_size);
    if (buffer == NULL) {
        fclose(dest_file);
        return abort_operation(fhandle);
    }

    ssize_t r;

    r = tfs_read(fhandle, buffer, inode->i_size);
    if (r == -1) {
        fclose(dest_file);
        return abort_operation(fhandle);
    }

    if (fputs(buffer, dest_file) == -1) {
        free(buffer);
        fclose(dest_file);
        return abort_operation(fhandle);
    }

    free(buffer);
    fclose(dest_file);
    tfs_close(fhandle);

    return 0;
}