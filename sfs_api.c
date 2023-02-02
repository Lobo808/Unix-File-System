#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"

// Cache directory
Dir_entry directory[MAX_FILE_NUM];
// Cache i-nodes
I_node inodes[MAX_FILE_NUM];
// Cache free bit map (0 = empty, 1 = used)
int free_bit_map[BLOCK_NUM] = {[0 ... BLOCK_NUM-1] = 0};
// Cache file descriptor table
File_descriptor fd[MAX_FILE_NUM];
// Keep track of current file
int ptr = 0;
int current_file = 0;
// Number of blocks for directory and inodes
int dir_block_num;
int inode_block_num;
// Indirect block
int indirect_block[BLOCK_SIZE_SFS / sizeof(int)];

Super_block sb;

// Update on disk: superblock, inodes, directory, free bit map
void update_disk()
{
	write_blocks(0, 1, &sb);
	write_blocks(1, inode_block_num, &inodes);
	write_blocks(inodes[0].dir_ptrs[0], dir_block_num, &directory);
	write_blocks(1020, 4, &free_bit_map);
}

int getIndexByName(const char *filename) {
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (strcmp(directory[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

int getFreeBlockIndex() {
	int index = 0;
    while (index < BLOCK_NUM && free_bit_map[index] == 1) {
        index++;
    }
    // No more space
    if (index == BLOCK_NUM) {
        return -1;
    }
    return index;
}


// Main
void mksfs(int flag) {

	if (flag) {
		init_fresh_disk("sun.sfs", BLOCK_SIZE_SFS, BLOCK_NUM);

		// Make sure cache directory, inodes, file descriptors are empty
        for (int i = 0; i < MAX_FILE_NUM; i++) {
            // directory
            memset(directory[i].filename, 0, MAXFILENAME);
            directory[i].inode = -1;

            // inodes
            inodes[i].mode = 0;
            inodes[i].link_cnt = 0;
            inodes[i].size = 0;
            for (int j = 0; j < 12; j++) {
                inodes[i].dir_ptrs[j] = -1;
            }
            inodes[i].ind_ptr = -1;

            // file descriptors
            fd[i].inode = -1;
            fd[i].ptr = -1;
        }

		// Calculate #block for inodes
        int extra = sizeof(inodes) % BLOCK_SIZE_SFS;
        if (extra != 0) {
            inode_block_num = (sizeof(inodes) / BLOCK_SIZE_SFS) + 1;
        }
        else {
            inode_block_num = (sizeof(inodes) / BLOCK_SIZE_SFS);
        }

		// Calculate #block for directory
        extra = sizeof(directory) % BLOCK_SIZE_SFS;
        if (extra != 0) {
            dir_block_num = (sizeof(directory) / BLOCK_SIZE_SFS) +1;
        }
        else {
            dir_block_num = sizeof(directory) / BLOCK_SIZE_SFS;
        }

		// Write superblock
		sb.magic = 0xACBD0005;
		sb.block_size = BLOCK_SIZE_SFS;
		sb.file_size = BLOCK_NUM * BLOCK_SIZE_SFS;
		sb.inode_table_length = inode_block_num;
		sb.dir = 0;

		// Update directory inode
        inodes[0].mode = 1;
        inodes[0].link_cnt = dir_block_num;
        int next_free_block = inode_block_num + 1;
        for (int i = 0; i < dir_block_num; i++) {
            inodes[0].dir_ptrs[i] = next_free_block;
            next_free_block++;
        }

		// Write free bit map
        int taken = 1 + inode_block_num + dir_block_num; // superblock, inodes, directory
        for (int i = 0; i < taken; i++) {
            free_bit_map[i] = 1;
        }

		update_disk();
	}
	else {
		init_disk("sun.sfs", BLOCK_SIZE_SFS, BLOCK_NUM);

		// Read superblock
		read_blocks(0, 1, &sb);

		// Read inodes
		inode_block_num = sb.inode_table_length;
		read_blocks(1, inode_block_num, &inodes);

		// Read directory
		dir_block_num = inodes[0].link_cnt;
		read_blocks(inode_block_num + 1, dir_block_num, &directory);

		// Read free bit map
		read_blocks(1020, 4, &free_bit_map);
	}
}

int sfs_getnextfilename(char *filename) {
	// Empty directory
    if (inodes[0].size == 0) {
        return 0;
    }
	// Reach end of directory
    if (current_file == inodes[0].size) {
        current_file = 0;
        ptr = 0;
        return 0;
    }
	else {
        while (directory[ptr].inode == -1) {
            ptr++;
            // No more file after
            if (ptr == MAX_FILE_NUM) {
                return 0;
            }
        }
		memcpy(filename, directory[ptr].filename, 33);
        current_file++;
        ptr = current_file;
        return 1;
    }
}

int sfs_getfilesize(const char *path) {
	int index = getIndexByName(path);
	if (index == -1) {
		return -1;
	}
	return inodes[directory[index].inode].size;
}

int sfs_fopen(char *filename){

	if (strlen(filename) > MAXFILENAME || strlen(filename) < 0) {
        return -1;
    }
    
    int index = getIndexByName(filename);

	// Requested file exists
	if (index != -1) {
		int inode_index = directory[index].inode;

        // Check if file has been opened
		for (int i = 0; i < MAX_FILE_NUM; i++) {
            if (fd[i].inode == inode_index) {
                return -1;
            }
        }

		// Check if file descriptor table has more space
		int fd_index = 0;
		while (fd[fd_index].inode != -1 && fd_index < MAX_FILE_NUM) {
			fd_index++;
		}
		if (fd_index < 0 || fd_index == MAX_FILE_NUM) {
			return -1;
		}

		// Set file descriptor
        fd[fd_index].inode = inode_index;
        fd[fd_index].ptr = inodes[inode_index].size; // Pointer at the end

		return fd_index;
	}

	// Requested file does not exist
	else {
		// Check if directory has more space
		int dir_index = 0;
        while(dir_index < MAX_FILE_NUM && directory[dir_index].inode != -1) {
            dir_index++;
        }
		if (dir_index == MAX_FILE_NUM) {
            return -1;
        }

		// Check if inode table has more space
		int inode_index = 0;
		while (inodes[inode_index].mode != 0 && inode_index < MAX_FILE_NUM) {
			inode_index++;
		}
		if (inode_index < 0 || inode_index == MAX_FILE_NUM) {
			return -1;
		}
		inodes[0].size++;

		// Set in directory
		directory[dir_index].inode = inode_index;
		memcpy(directory[dir_index].filename, filename, strlen(filename) + 1);

		// Set in inode table
		inodes[inode_index].mode = 1;

		// Update on disk
		update_disk();

		// Check if file descriptor table has more space
		int fd_index = 0;
		while (fd[fd_index].inode != -1 && fd_index < MAX_FILE_NUM) {
			fd_index++;
		}
		if (fd_index < 0 || fd_index == MAX_FILE_NUM) {
			return -1;
		}

		// Set in file descriptor table
		fd[fd_index].inode = inode_index;
		fd[fd_index].ptr = 0;

		return fd_index;
	}
}

int sfs_fclose(int fileID) {
	// Not a valid fileID
    if (fileID >= MAX_FILE_NUM || fileID < 0 || fd[fileID].inode == -1) {
        return -1;
    }
	// Remove from file descriptor table
	fd[fileID].inode = -1;
	fd[fileID].ptr = -1;

	return 0;
}

int sfs_fwrite(int fileID, const char *buf, int length) {
	// Not a valid fileID
    if (fileID >= MAX_FILE_NUM || fileID < 0 || fd[fileID].inode == -1) {
        return -1;
    }

	int write_size = length;
    int extra_block_num;
    int inode_index = fd[fileID].inode;
    int size = inodes[inode_index].size;
    int ptr = fd[fileID].ptr;

	// Check if completely writable (check length)
        // Reach max size
    if (ptr + length > FILE_SIZE) {
        write_size = FILE_SIZE - ptr;
        size = FILE_SIZE;
        if (write_size == 0) {
            return 0;
        } 
		int extra = write_size % BLOCK_SIZE_SFS;
        if (extra != 0) {
            extra_block_num = (write_size / BLOCK_SIZE_SFS) + 1;
        }
        else {
            extra_block_num = write_size / BLOCK_SIZE_SFS;
        }
	}
	    // No new blocks needed
    else if (ptr + length <= size) {
        extra_block_num = 0;
    }
		// Increase block size
    else {
        size = ptr + length;
        int extra = size % BLOCK_SIZE_SFS;
        if (extra != 0) {
            extra_block_num = (size / BLOCK_SIZE_SFS) + 1;
        }
        else {
            extra_block_num = size / BLOCK_SIZE_SFS;
        }
        extra_block_num -= inodes[inode_index].link_cnt;
    }

	// Manage indirect pointer
	void *indirect = malloc(BLOCK_SIZE_SFS);
		// Create indirect pointer
	if (size > (12 * BLOCK_SIZE_SFS) && inodes[inode_index].ind_ptr == -1) {
		for (int i = 0; i < BLOCK_SIZE_SFS / sizeof(int); i++) {
		    indirect_block[i] = -1;
	    }
		int free_block_index = getFreeBlockIndex();
		// No more space
		if (free_block_index == -1) {
			size = 12 * BLOCK_SIZE_SFS;
			write_size = size - ptr;
			int extra = write_size % BLOCK_SIZE_SFS;
            if (extra != 0) {
                extra_block_num = (write_size / BLOCK_SIZE_SFS) + 1;
            }
            else {
                extra_block_num = write_size / BLOCK_SIZE_SFS;
            }
		}
		// Allocate block
		else {
			inodes[inode_index].ind_ptr = free_block_index;
			free_bit_map[free_block_index] = 1;
		}
	}
	else if (size > (12 * BLOCK_SIZE_SFS) && inodes[inode_index].ind_ptr != -1) {
		read_blocks(inodes[inode_index].ind_ptr, 1, indirect);
		memcpy(indirect_block, indirect, BLOCK_SIZE_SFS);
	}
	free(indirect);

	// Use new data blocks
	if (extra_block_num > 0) {
		for (int i = inodes[inode_index].link_cnt; i < inodes[inode_index].link_cnt + extra_block_num; i++) {
			int free_block_index = getFreeBlockIndex();
			// No more available block
            if (free_block_index == -1) {
				extra_block_num = inodes[inode_index].link_cnt - i;
				break;
			}
			// Direct pointer
			if (i < 12) {
				inodes[inode_index].dir_ptrs[i] = free_block_index;
			}
			// Indirect pointer
			else {
				indirect_block[i - 12] = free_block_index;
			}
			// Update free bit map
            free_bit_map[free_block_index] = 1;
		}
	}

	// Calculate start block and offset
	int start_block = ptr / BLOCK_SIZE_SFS;
    int offset = ptr % BLOCK_SIZE_SFS;

	void *buffer = malloc(BLOCK_SIZE_SFS * (inodes[inode_index].link_cnt + extra_block_num));
	int current_block = start_block;
	while (current_block < inodes[inode_index].link_cnt + extra_block_num) {
		if (current_block < 12) {
			read_blocks(inodes[inode_index].dir_ptrs[current_block], 1, &buffer[(current_block - start_block) * BLOCK_SIZE_SFS]);
		}
		else {
			read_blocks(indirect_block[current_block - 12], 1, &buffer[(current_block - start_block) * BLOCK_SIZE_SFS]);
		}
		current_block++;
	}

	// Replace with new content
	memcpy(&buffer[offset], buf, write_size);

	// Update inode
	inodes[inode_index].link_cnt += extra_block_num;
	inodes[inode_index].size = size;

	// Write data blocks on disk
	current_block = start_block;
	while (current_block < inodes[inode_index].link_cnt) {
		if (current_block < 12) {
			write_blocks(inodes[inode_index].dir_ptrs[current_block], 1, buffer);
		}
		else {
			write_blocks(indirect_block[current_block - 12], 1, &buffer[(current_block - start_block) * BLOCK_SIZE_SFS]);
		}
		current_block++;
	}

	free(buffer);

	// Update pointer in file descriptor table
    fd[fileID].ptr += write_size;

	// Write indirect pointer block to disk
	if (inodes[inode_index].link_cnt > 12) {
		write_blocks(inodes[inode_index].ind_ptr, 1, &indirect_block);
	}

	// Update on disk
	update_disk();

	return write_size;
}

int sfs_fread(int fileID, char *buf, int length) {
	// Not a valid fileID
    if (fileID >= MAX_FILE_NUM || fileID < 0 || fd[fileID].inode == -1) {
        return -1;
    }

	int read_size = length;
    int inode_index = fd[fileID].inode;
    int size = inodes[inode_index].size;
    int ptr = fd[fileID].ptr;

	if (size == 0 || length == 0) {
        return 0;
    }

	// Check if completely readable
    if (ptr + length > size) {
        read_size = size - ptr;
    }

	// Calculate start block and offset
	int start_block = ptr / BLOCK_SIZE_SFS;
    int offset = ptr % BLOCK_SIZE_SFS;

	// Indirect block
	void *indirect = malloc(BLOCK_SIZE_SFS);
	if (inodes[inode_index].ind_ptr != -1) {
		read_blocks(inodes[inode_index].ind_ptr, 1, indirect);
		memcpy(&indirect_block, indirect, BLOCK_SIZE_SFS);
	} 
	free(indirect);

	// Read from data blocks
	void *buffer = malloc(BLOCK_SIZE_SFS * inodes[inode_index].link_cnt);
	int current_block = start_block;
	while (current_block < inodes[inode_index].link_cnt) {
		if (current_block < 12) {
			read_blocks(inodes[inode_index].dir_ptrs[current_block], 1, &buffer[(current_block - start_block) * BLOCK_SIZE_SFS]);
		}
		else {
			read_blocks(indirect_block[current_block - 12], 1, &buffer[(current_block - start_block) * BLOCK_SIZE_SFS]);
		}
		current_block++;
	}

	// Copy read content to buffer
	memcpy(buf, &buffer[offset], read_size);
	free(buffer);

	// Update pointer location
	fd[fileID].ptr += read_size;

	return read_size;
}

int sfs_fseek(int fileID, int loc) {
	// Not a valid fileID
    if (fileID >= MAX_FILE_NUM || fileID < 0 || fd[fileID].inode == -1) {
        return -1;
    }
	
	// Not a valid location
    if (loc < 0 || loc > inodes[fd[fileID].inode].size) {
        return -1;
    }

	fd[fileID].ptr = loc;
	return 0;
}

int sfs_remove(char *file) {
	int index = getIndexByName(file);

    // File not in directory
    if (index == -1) {
        return -1;
    }

	int inode_index = directory[index].inode;

	// Check if the file has been opened
	for (int i = 0; i < MAX_FILE_NUM; i++) {
		if (fd[i].inode == inode_index) {
			return -1;
		}
	}

	// Remove indirect pointer and associated blocks
	if (inodes[inode_index].ind_ptr != -1) {
		char buffer[BLOCK_SIZE_SFS];
		read_blocks(inodes[inode_index].ind_ptr, 1, buffer);
		memcpy(&indirect_block, &buffer, BLOCK_SIZE_SFS);

		// Remove associated blocks
		for (int i = 12; i < inodes[inode_index].link_cnt; i++) {
            free_bit_map[indirect_block[i-12]] = 0;
        }

		// Remove indirect block
		free_bit_map[inodes[inode_index].ind_ptr] = 0;
	}

	// Remove direct blocks
	for (int i = 0; i < inodes[inode_index].link_cnt && i < 12; i++) {
        free_bit_map[inodes[inode_index].dir_ptrs[i]] = 0;
    }

	// Remove from directory
	directory[index].inode = -1;
	memset(directory[index].filename, 0, sizeof(directory[index].filename));

	// Remove from inode table, decrease directory size
	inodes[inode_index].mode = 0;
	inodes[inode_index].link_cnt = 0;
	inodes[inode_index].size = 0;
	inodes[inode_index].ind_ptr = -1;
	for (int i = 0; i < 12; i++)
	{
		inodes[inode_index].dir_ptrs[i] = -1;
	}
	inodes[0].size -= 1;

	// Update disk
	update_disk();

	return 0;
}
