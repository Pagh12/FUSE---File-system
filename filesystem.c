        #include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include<stdbool.h>
#include <utime.h>
#include <sys/time.h>

int dm510fs_getattr( const char *, struct stat * );
int dm510fs_readdir( const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info * );
int dm510fs_open( const char *, struct fuse_file_info * );
int dm510fs_read( const char *, char *, size_t, off_t, struct fuse_file_info * );
int dm510fs_release(const char *path, struct fuse_file_info *fi);
int dm510fs_mkdir(const char *path, mode_t mode);
void* dm510fs_init();
void dm510fs_destroy(void *private_data);
int dm510fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int dm510fs_unlink(const char *path);
int dm510fs_rmdir(const char *path);
int dm510fs_mknod(const char *path, mode_t mode, dev_t rdev);
int dm510fs_utime(const char *path, struct utimbuf *time);
int dm510fs_truncate(const char *path, off_t size);
int loadFileSystem(const char *filename);
int saveFileSystem(const char *filename);
void encryptCaesarCypher(char *text, int length);
void decryptCaesarCypher(char *text, int length);
int blocks_needed(size_t size);
int find_free_block();

/*
 * See descriptions in fuse source code usually located in /usr/include/fuse/fuse.h
 * Notice: The version on Github is a newer version than installed at IMADA
 */
static struct fuse_operations dm510fs_oper = {
	.getattr	= dm510fs_getattr,
	.readdir	= dm510fs_readdir,
	.mknod = dm510fs_mknod,
	.mkdir = dm510fs_mkdir,
	.unlink = dm510fs_unlink,
	.rmdir = dm510fs_rmdir,
	.truncate = dm510fs_truncate,
	.open	= dm510fs_open,
	.read	= dm510fs_read,
	.release = dm510fs_release,
	.write = dm510fs_write,
	.rename = NULL,
	.utime = dm510fs_utime,
	.init = dm510fs_init,
	.destroy = dm510fs_destroy
};

#define MAX_DATA_IN_FILE 256
#define MAX_PATH_LENGTH  256
#define MAX_NAME_LENGTH  256
#define MAX_INODES  4



// amount of shifts to encrypt and decrypt
int shift; 

// struct for a block
#define BLOCK_SIZE 8
#define BLOCKS_COUNT 10000
typedef struct Block{
	char data[BLOCK_SIZE];
	bool is_free;
	size_t size; // how much of the block is full 
	bool is_full;
} Block;

Block blocks[BLOCKS_COUNT]; // 0 :


#define MAX_INODE_BLOCKS 4
/* The Inode for the filesystem*/
typedef struct Inode {
	bool is_active;
	bool is_dir;
	Block* blocks[MAX_INODE_BLOCKS]; // 
	char path[MAX_PATH_LENGTH];
	char name[MAX_NAME_LENGTH];
	mode_t mode;
	nlink_t nlink;
	time_t a_time;
	time_t m_time;
} Inode;

Inode filesystem[MAX_INODES];



void debug_inode(int i) {
	Inode inode = filesystem[i];

	printf("=============================================\n");
	printf("      Path: %s\n", inode.path);
	printf("=============================================\n");
}

/*
 * Return file attributes.
 * The "stat" structure is described in detail in the stat(2) manual page.
 * For the given pathname, this should fill in the elements of the "stat" structure.
 * If a field is meaningless or semi-meaningless (e.g., st_ino) then it should be set to 0 or given a "reasonable" value.
 * This call is pretty much required for a usable filesystem.
*/
int dm510fs_getattr(const char *path, struct stat *stbuf) {
	printf("getattr: (path=%s)\n", path);

	memset(stbuf, 0, sizeof(struct stat));
	for(int i = 0; i < MAX_INODES; i++) {
		printf("===> %s  %s \n", path, filesystem[i].path);
		if(filesystem[i].is_active && strcmp(filesystem[i].path, path) == 0) {
			printf("Found inode for path %s at location %i\n", path, i);
			stbuf->st_mode = filesystem[i].mode;
			stbuf->st_nlink = filesystem[i].nlink;

			// calculates the size for blocks
			off_t total_size = 0; 
			for (int j = 0; j< MAX_INODE_BLOCKS; j++){
				if(filesystem[i].blocks[j] != NULL){
					total_size = total_size + filesystem[i].blocks[j]->size;
				}

			}
			stbuf->st_size = total_size;
			return 0;
		}
	}
	printf("getattr: Max number of inodes used\n");
	return -ENOENT;
}

/*
 * Return one or more directory entries (struct dirent) to the caller. This is one of the most complex FUSE functions.
 * Required for essentially any filesystem, since it's what makes ls and a whole bunch of other things work.
 * The readdir function is somewhat like read, in that it starts at a given offset and returns results in a caller-supplied buffer.
 * However, the offset not a byte offset, and the results are a series of struct dirents rather than being uninterpreted bytes.
 * To make life easier, FUSE provides a "filler" function that will help you put things into the buffer.
 *
 * The general plan for a complete and correct readdir is:
 *
 * 1. Find the first directory entry following the given offset (see below).
 * 2. Optionally, create a struct stat that describes the file as for getattr (but FUSE only looks at st_ino and the file-type bits of st_mode).
 * 3. Call the filler function with arguments of buf, the null-terminated filename, the address of your struct stat
 *    (or NULL if you have none), and the offset of the next directory entry.
 * 4. If filler returns nonzero, or if there are no more files, return 0.
 * 5. Find the next file in the directory.
 * 6. Go back to step 2.
 * From FUSE's point of view, the offset is an uninterpreted off_t (i.e., an unsigned integer).
 * You provide an offset when you call filler, and it's possible that such an offset might come back to you as an argument later.
 * Typically, it's simply the byte offset (within your directory layout) of the directory entry, but it's really up to you.
 *
 * It's also important to note that readdir can return errors in a number of instances;
 * in particular it can return -EBADF if the file handle is invalid, or -ENOENT if you use the path argument and the path doesn't exist.
*/
int dm510fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;
	printf("readdir: (path=%s)\n", path);

	for(int i = 0; i < MAX_INODES; i++) {
        // Check if the current inode is a subdirectory of the given path
        if(strncmp(path, filesystem[i].path, strlen(path)) == 0 && strlen(filesystem[i].path) > strlen(path)) {
            char *dir_name = filesystem[i].path + strlen(path);

			if (dir_name[0] == '/') {
                dir_name++; // Skip the leading '/'
             }
			int slashcount= 0;
			for (int j = 0; j < strlen(dir_name); j++){
				if(dir_name[j]== '/'){
					slashcount += 1;
				}
			}
			if(slashcount == 0){
            	// Add the subdirectory to the directory listing
            	filler(buf, dir_name, NULL, 0);
			}
        }
    }
	return 0;
}

/*
 * Open a file.
 * If you aren't using file handles, this function should just check for existence and permissions and return either success or an error code.
 * If you use file handles, you should also allocate any necessary structures and set fi->fh.
 * In addition, fi has some other fields that an advanced filesystem might find useful; see the structure definition in fuse_common.h for very brief commentary.
 * Link: https://github.com/libfuse/libfuse/blob/0c12204145d43ad4683136379a130385ef16d166/include/fuse_common.h#L50
*/
int dm510fs_open(const char *path, struct fuse_file_info *fi) {
    printf("open: (path=%s)\n", path);
	return 0;
}

/*
 * Read size bytes from the given file into the buffer buf, beginning offset bytes into the file. See read(2) for full details.
 * Returns the number of bytes transferred, or 0 if offset was at or beyond the end of the file. Required for any sensible filesystem.
*/
int dm510fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read: (path=%s)\n", path);

	// Pointer to store the inode corresponding to the path
    Inode *inode = NULL;

	// Search for the inode in the filesystem by comparing the path
	for(int i = 0; i < MAX_INODES; i++) {
		if(strcmp(filesystem[i].path, path) == 0 ) {
            inode = &filesystem[i];
        }
    }
	// If inode is not found, return error code -ENOENT
    if(inode == NULL){
        return -ENOENT;
    }
	// Calculate the block index and the offset within the block
    off_t blockIndex = offset/BLOCK_SIZE; 
	if(blockIndex >= MAX_INODE_BLOCKS){
		return -ENOSPC; // Return error if the block index exceeds the maximum number of blocks
	}
	// Number of bytes left to read
    size_t bytesLeft = size;
	// Calculate the offset within the block
    off_t blockOffset = offset% BLOCK_SIZE;
	
	// Read loop to handle reading across multiple blocks if necessary
    while (bytesLeft > 0 && blockIndex < MAX_INODE_BLOCKS){
		// Get the current block
        Block *block = inode->blocks[blockIndex];

        if(block==NULL){
            return size - bytesLeft;
        }

		// Determine the number of bytes to read from the current block
        size_t blockRead;

        if(bytesLeft > BLOCK_SIZE - offset){
            blockRead = BLOCK_SIZE - offset; 
        }
        else{
            blockRead = bytesLeft;
        }
		
		// Copy data from the block to the buffer and decrypt it
        memcpy(buf, block->data + blockOffset, blockRead );
		decryptCaesarCypher(buf, blockRead);

		// Move the buffer pointer and update counters
        buf += blockRead;
        offset = 0;  // Reset offset for more blocks
        bytesLeft -= blockRead;
        blockIndex += 1;

    }
	// Return the number of bytes read
    return size-bytesLeft;
    

}

/*
 * Makes a directory
*/
int dm510fs_mkdir(const char *path, mode_t mode) {
	printf("mkdir: (path=%s)\n", path);

	// Locate the first unused Inode in the filesystem
	for(int i = 0; i < MAX_INODES; i++) {
		if(filesystem[i].is_active == false) {
			printf("mkdir: Found unused inode for at location %i\n", i);
			// Use that for the directory
			filesystem[i].is_active = true;
			filesystem[i].is_dir = true;
			filesystem[i].mode = S_IFDIR | 0755;
			filesystem[i].nlink = 2;
			memcpy(filesystem[i].path, path, strlen(path) + 1);
			
			// Uses last part of path as the inode name
			char *dir = strrchr(path, '/');
			strcpy(filesystem[i].name, dir + 1);

			debug_inode(i);
			return 0;
		}
	}
	printf("mkdir: No more space in filesystem\n");
	return -ENOSPC;
}

/*
 * This is the only FUSE function that doesn't have a directly corresponding system call, although close(2) is related.
 * Release is called when FUSE is completely done with a file; at that point, you can free up any temporarily allocated data structures.
 */
int dm510fs_release(const char *path, struct fuse_file_info *fi) {
	printf("release: (path=%s)\n", path);
	return 0;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the `private_data` field of
 * `struct fuse_context` to all file operations, and as a
 * parameter to the destroy() method. It overrides the initial
 * value provided to fuse_main() / fuse_new().
 */
void* dm510fs_init() {
    printf("init filesystem\n");

	// Loop through all inodes - set them inactive
	for(int i = 0; i < MAX_INODES; i++) {
		filesystem[i].is_active = false;
        for ( int k = 0 ; k< MAX_INODE_BLOCKS; k++){
			// Initialize all inode blocks as NULL
            filesystem[i].blocks[k] = NULL;
        }
	}

	// Add root inode 
	filesystem[0].is_active = true;
	filesystem[0].is_dir = true;
	filesystem[0].mode = S_IFDIR | 0755;
	filesystem[0].nlink = 2;
	memcpy(filesystem[0].path, "/", 2); 

	// Initialize all blocks as free
	for(int j= 0 ; j< BLOCKS_COUNT; j++){
		blocks[j].is_free = true;
        
        }


	loadFileSystem("saveFile.txt");

    return NULL;
}

/**
 * Clean up filesystem
 * Called on filesystem exit.
 */
void dm510fs_destroy(void *private_data) {

	saveFileSystem("saveFile.txt");

	printf("file saved.\n");
    printf("destroy filesystem\n");
}

/*
 * Makes a file 
*/
int dm510fs_mknod(const char *path, mode_t mode, dev_t rdev){
	printf("mknod: (path=%s)\n", path);

	// Locate the first unused Inode in the filesystem
	for(int i = 0; i < MAX_INODES; i++) {
		if(filesystem[i].is_active == false) {

			printf("mknod: Found unused inode for at location %i\n", i);
			// Use that for the directory
			filesystem[i].is_active = true;
			filesystem[i].is_dir = false;
			filesystem[i].mode = mode | S_IFREG;
			filesystem[i].nlink = 1;
			memcpy(filesystem[i].path, path, strlen(path)+1);

			// Uses last part of path as the inode name
			char *dir = strrchr(path, '/');
			strcpy(filesystem[i].name, dir + 1);

			debug_inode(i);
			return 0;
		}

	}
	printf("mknod: No more space in filesystem\n");
	return -ENOSPC;
}

/*
 * Function that tells the time of an action
*/
int dm510fs_utime(const char *path, struct utimbuf *time){
	printf("utime: %s %ld %ld \n", path, time->actime, time->modtime);

	// Adds the times to every inode
	for(int i = 0; i < MAX_INODES; i++){
		if(strcmp(path, filesystem[i].path)){
			filesystem[i].a_time = time->actime;
			filesystem[i].m_time = time->modtime;
			return 0;
		}
	}
	printf("utime: Path not found\n");
	return -ENOENT; 
}

/*
 * Writes to a file
*/
int dm510fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	printf("write: (path=%s)\n", path);
    // Pointer to store the inode corresponding to the path
	Inode *inode = NULL;

	// Search for the inode in the filesystem by comparing the path
	for(int i = 0; i < MAX_INODES; i++)
		if(strcmp(filesystem[i].path, path) == 0 && filesystem[i].is_dir == false)
            inode = &filesystem[i];
	// If inode is not found, return error code -ENOENT
    if(inode == NULL){
        return -ENOENT;
    }

	// Calculate the block index and the offset within the block
    int blockIndex = offset/BLOCK_SIZE;
    if (blockIndex >= MAX_INODE_BLOCKS){
		return -ENOSPC;  // Return error if the block index exceeds the maximum number of blocks
	}

	off_t blockOffset = offset % BLOCK_SIZE;

	// Number of bytes left to write
    size_t bytesLeft = size; 

	// Write loop to handle writing across multiple blocks if necessary
    while (bytesLeft > 0 && blockIndex < MAX_INODE_BLOCKS){
		
        Block *block = inode->blocks[blockIndex];

		// If the block is not allocated, find a free block
        if(block==NULL){
            int blockfree = find_free_block();
			// Check if there are no free blocks available
            if(blockfree < 0 ){
				// Return error if nothing has been written, otherwise return the amount written
                if(bytesLeft == size ){
                    return -ENOSPC;
                }
                else{
                    return size - bytesLeft; 
                }
            }

			// Allocate the new block and update the inode
            block = &blocks[blockfree];
            block->is_free = false; 
            inode->blocks[blockIndex] = block; 
        }
		// Determine the number of bytes to write to the current block
        size_t blockWrite;

        if(bytesLeft > BLOCK_SIZE - offset){
            blockWrite = BLOCK_SIZE - offset; 
        }
        else{
            blockWrite = bytesLeft;
        }
		// Copy data to the block and encrypt it
        memcpy(block->data + blockOffset, buf, blockWrite );
		encryptCaesarCypher(block->data + blockOffset, blockWrite );

		// Update the block size
		block->size = blockOffset + blockWrite;

		// Move the buffer pointer and update counters
        buf += blockWrite;
        offset = 0; // Reset offset for new blocks
        bytesLeft -= blockWrite;
        blockIndex += 1;

    }
	printf("write size : %d", size);
	// Return the number of bytes written
    return size-bytesLeft;

}

/*
 * Deletes a file
*/
int dm510fs_unlink(const char *path){
	printf("path unlink: %s", path);

	for(int i = 0; i < MAX_INODES; i++){
		if(strcmp(filesystem[i].path, path) == 0 && filesystem[i].is_dir == false){
			filesystem[i].is_active = false;
			filesystem[i].nlink = 0;
			filesystem[i].path[0] = '\0';
			return 0; 
		}
	}
	printf("unlink: Path not found\n");
	return -ENOENT;
}

/*
 * Deletes a directory
*/
int dm510fs_rmdir(const char *path){
	printf("rmdir: (path=%s)\n", path);

	for (int i = 0; i < MAX_INODES; i++){
        if (strcmp(filesystem[i].path, path) == 0 && filesystem[i].is_dir == true) {

			// Checks if there is anything in the directory
			for(int j = 0; j < MAX_INODES; j++ ){
				if (strncmp(filesystem[j].path, path, strlen(path)) == 0 && 
					strlen(filesystem[j].path) > strlen(path)){
						printf("Error deleting directory, existing subdirectory or file: %s \n",filesystem[j].path);
						return -ENOTEMPTY;
				}
			}
			filesystem[i].is_active = false;
			filesystem[i].nlink = 0;
			filesystem[i].path[0] = '\0';
			return 0;
        }
	}
	printf("rmdir: Directory not found\n");	
	return -ENOENT;
}

/*
 * Resizes file
*/
int dm510fs_truncate(const char *path, off_t size){
	printf("truncate: (path=%s)\n", path);
	// Pointer to store the inode corresponding to the path
    Inode *inode= NULL; 

	// Search for the inode in the filesystem by comparing the path
	for (int i = 0; i<MAX_INODES; i++){
		if (strcmp(filesystem[i].path, path) == 0 && filesystem[i].is_dir == false){
            inode = &filesystem[i];
		}

	}
	// If inode is not found, return error code -ENOENT
    if( inode == NULL){
        return -ENOENT;
    }
	// Calculate the number of blocks needed to store the specified size
    int remainingBlocks = (size - 1) / BLOCK_SIZE + 1;

	// Free blocks that are beyond the required size
    for(int j = remainingBlocks; j < MAX_INODE_BLOCKS; j++){
        if(inode->blocks[j] != NULL){
            inode->blocks[j]->is_free = true;
        }
        inode->blocks[j] = NULL;

    }

	// Adjust the size of the last required block
	if(remainingBlocks == 0 ){
		inode->blocks[remainingBlocks-1]->size = size % BLOCK_SIZE;
	}

    return 0; 
}

/*
 * Function to save the file system state to a file
*/
int saveFileSystem(const char *filename) {
	// Open file for writing in binary mode
    FILE *file = fopen(filename, "wb"); 
    if (file == NULL) {
        perror("Error opening file for writing\n");
        return -ENOENT;
    }

    // Write the entire file system state to the file
    if (fwrite(filesystem, sizeof(filesystem), 1, file) != 1) {
        perror("Error writing file system state\n");
        fclose(file);
		return -EIO;
	}

    fclose(file);
    return 0;
}


/*
 * Function to load the file system state from a file
*/
int loadFileSystem(const char *filename) {
	// Open file for reading in binary mode
    FILE *file = fopen(filename, "rb"); 
    if (file == NULL) {
        perror("Error opening file for reading\n");
        return -ENOENT;
    }

    // Read the file system state from the file
    if (fread(filesystem, sizeof(filesystem), 1, file) != 1) {
        perror("Error reading file system state\n");
        fclose(file);
        return -EIO;
    }

    fclose(file);
    return 0;
}

/*
 * Caesar encryption of file contents
*/ 
void encryptCaesarCypher(char *text, int length){

	for (int i = 0; i< length; i++){
		char c = text[i];
		
		
		// Encrypt uppercase letters
        if (c >= 'A' && c <= 'Z') {
            text[i] = ((c - 'A' + shift) % 26) + 'A';
        }
        
		// Encrypt lowercase letters
        else if (c >= 'a' && c <= 'z') {
            text[i] = ((c - 'a' + shift) % 26) + 'a';
        }

	}
}

/*
 * Caesar decryption of file contents
*/ 
void decryptCaesarCypher(char *text, int length){
	for(int i = 0; i< length; i++){
		char c = text[i];

		// Decrypt uppercase
		if (c >= 'A' && c <= 'Z') {
			text[i] = ((c - 'A' - shift) % 26) + 'A';
		}

		// Decrypt lowercase letters
        else if (c >= 'a' && c <= 'z') {
            text[i] = ((c - 'a' - shift) % 26) + 'a';

        }
	}
}

/*
 * Helper function for finding free blocks of data
*/ 
int find_free_block() {
    for (int i = 0; i < BLOCKS_COUNT; i++) {
        if (blocks[i].is_free) {
            // Found a free block, return its index
            return i;
        }
    }
    // No free block found, return -1
    return -1;
}

/*
 * Helper function for finding amount of blocks needed
*/ 
int blocks_needed(size_t size){
	int sum = 0; 
	while ( size > BLOCK_SIZE * sum){
		sum = sum + 1; 
	}
	return sum;
}



int main( int argc, char *argv[] ) {
    if(argc > 1){
        shift = atoi(argv[argc - 1]); 
    } else {
        printf("Shift value not provided. Using default shift of 0.\n");
        shift = 0;
    }
	fuse_main( argc-1, argv, &dm510fs_oper );
	return 0;
}
    

    

    
