#ifndef SFS_API_H
#define SFS_API_H

#define BLOCK_SIZE_SFS 1024 // number of bytes per block
#define BLOCK_NUM 1024  // number of blocks
#define FILE_SIZE ((12*BLOCK_SIZE_SFS) + ((BLOCK_SIZE_SFS/sizeof(int)) *BLOCK_SIZE_SFS)) // maximum file size (int is 4 bytes)
#define MAX_FILE_NUM  100 // maximum number of files in the file system
#define MAXFILENAME 32


// 1024 bytes per block
typedef struct BLOCK {
    char data[BLOCK_SIZE_SFS];
} Block;

// takes the first block
typedef struct SUPER_BLOCK {
    int magic;
    int block_size;
    int file_size;
    int inode_table_length;
    int dir;
} Super_block;

// 64 bytes -> 16 i-nodes per block
typedef struct I_NODE {
    int mode;   // 0 = empty, 1 = used
    int link_cnt;
    int size;
    int dir_ptrs[12];   // -1 = empty
    int ind_ptr;        // -1 = empty
} I_node;

// 32 bytes -> 32 dir_entry per block
typedef struct DIR_ENTRY {
    char filename[33];      // filename is limited to 32
    int inode;
} Dir_entry;

typedef struct FILE_DESCRIPTOR {
    int inode;
    int ptr;
} File_descriptor;

// Helper
void update_disk();
int getIndexByName(const char*);
int getFreeBlockIndex();

// Main
void mksfs(int);
int sfs_getnextfilename(char*);
int sfs_getfilesize(const char*);
int sfs_fopen(char*);
int sfs_fclose(int);
int sfs_fwrite(int, const char*, int);
int sfs_fread(int, char*, int);
int sfs_fseek(int, int);
int sfs_remove(char*);

#endif