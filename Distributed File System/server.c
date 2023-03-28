#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BUFFER_SIZE (4096)

int sd, fd, image_size;
struct sockaddr_in addr;
void *image;
super_t *s;
unsigned int *inode_bitmap;
unsigned int *data_bitmap;
inode_t *inode_table;
inode_t *root_inode;
dir_ent_t *root_dir;

typedef struct __Stat_t {
    int type;   // MFS_DIRECTORY or MFS_REGULAR
    int size;   // bytes
    // note: no permissions, access times, etc.
} Stat_t;

typedef struct {
    dir_ent_t entries[128];
} dir_block_t;

void printFSInfo() {
    printf("inode bitmap address %d [len %d]\n", s->inode_bitmap_addr, s->inode_bitmap_len);
    printf("data bitmap address %d [len %d]\n", s->data_bitmap_addr, s->data_bitmap_len);
    printf("inode region address %d [len %d]\n", s->inode_region_addr, s->inode_region_len);
    printf("data region address %d [len %d]\n", s->data_region_addr, s->data_region_len);

    printf("\nnumber of inodes:%d number of data blocks:%d\n", s->num_inodes, s->num_data);

    printf("\nroot type:%d root size:%d\n", root_inode->type, root_inode->size);
    printf("direct pointers[0]:%d [1]:%d\n", root_inode->direct[0], root_inode->direct[1]);

    printf("\nroot dir entries\n%d %s\n", root_dir[0].inum, root_dir[0].name);
    printf("%d %s\n", root_dir[1].inum, root_dir[1].name);
    printf("%d %s\n", root_dir[2].inum, root_dir[2].name);
    printf("%d %s\n", root_dir[3].inum, root_dir[3].name);
    printf("%d %s\n", root_dir[4].inum, root_dir[4].name);
    printf("%d %s\n", root_dir[5].inum, root_dir[5].name);
    printf("%d %s\n", root_dir[6].inum, root_dir[6].name);
}

void intHandler(int dummy) {
    UDP_Close(sd);
    close(fd);
    exit(130);
}

unsigned int get_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   return (bitmap[index] >> offset) & 0x1;
}

void set_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   bitmap[index] |= 0x1 << offset;
}

void set_bit_zero(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   bitmap[index] |= 0x0 << offset;
}

/*
    int MFS_Lookup(int pinum, char *name): MFS_Lookup() takes the parent inode number (which should be the
    inode number of a directory) and looks up the entry name in it. The inode number of name is returned.
    Success: return inode number of name; failure: return -1. Failure modes: invalid pinum, name does not exist in pinum.
*/
void fsLookup(msg m) {
    int ret = -1;
    inode_t *parent_inode = inode_table + (m.pinum * sizeof(inode_t));
    printf("searching in dir with inode: %d\n", m.pinum);
    if (parent_inode->type != UFS_DIRECTORY) {
        ret = -1;
    } else {
        for(int i = 0; i < DIRECT_PTRS; i++) {
            int block_num = parent_inode->direct[i];
            printf("blocknum %d\n", block_num);
            if (block_num == -1) {
                break;
            }
            dir_block_t *dir = (dir_block_t*)(image + (block_num * UFS_BLOCK_SIZE));
            for (int j = 0; j < 128; j++) {
                if(dir->entries[j].inum != 0)
                    printf("  %d %s\n", dir->entries[j].inum, dir->entries[j].name);
                if((strcmp(dir->entries[j].name, m.name) == 0) && dir->entries[j].inum != -1) {
                    ret = dir->entries[j].inum;
                    break;
                }
            }
        }
    }
    UDP_Write(sd, &addr, (char *) &ret, BUFFER_SIZE);
}

/*
    int MFS_Stat(int inum, MFS_Stat_t *m): MFS_Stat() returns some information about the file specified by inum.
    Upon success, return 0, otherwise -1. The exact info returned is defined by MFS_Stat_t. Failure modes: inum
    does not exist. File and directory sizes are described below.

    typedef struct __MFS_Stat_t {
        int type;   // MFS_DIRECTORY or MFS_REGULAR
        int size;   // bytes
        // note: no permissions, access times, etc.
    } MFS_Stat_t;
*/
void fsStat(msg msg) {
    int ret = -1;

    // check that inode exists in inode bitmap
    if (get_bit(inode_bitmap, msg.inum) == 1) {
        // inode with corresponding inum
        inode_t *inode = inode_table + (msg.inum * sizeof(inode_t));

        // populate msg.m with inode.type and inode.size
        msg.stat_message_size = inode->size;
        msg.stat_message_type = inode->type;

        // set return to 0
        ret = 0;
    }
    msg.stat_return = ret;
    // udp write back to client
    UDP_Write(sd, &addr, (char *) &msg, sizeof(msg));
}

/*
    int MFS_Write(int inum, char *buffer, int offset, int nbytes): MFS_Write() writes a buffer of size nbytes (max size:
    4096 bytes) at the byte offset specified by offset. Returns 0 on success, -1 on failure. Failure modes: invalid inum,
    invalid nbytes, invalid offset, not a regular file (because you can't write to directories).

    Size: The size of a file is the offset of the last valid byte written to the file. Specifically, if you write
    100 bytes to an empty file at offset 0, the size is 100; if you write 100 bytes to an empty file at offset 10,
    the size is 110. For a directory, it is the same (i.e., the byte offset of the last byte of the last valid entry).
*/
void fsWrite(msg m) {
    int ret = -1;

    inode_t *curr_inode = inode_table + (m.inum * sizeof(inode_t));
    int valid_inum = get_bit(inode_bitmap, m.inum) == 1;
    int valid_nbytes = m.nbytes >= 0 && m.nbytes <= 4096;
    int valid_offset = m.offset >= 0 && m.offset <= curr_inode->size;
    int valid_type = curr_inode->type == MFS_REGULAR_FILE;
    int valid_block = (m.offset + m.nbytes > (2 * UFS_BLOCK_SIZE)) ? 0 : 1;

    if(valid_inum && valid_nbytes && valid_offset && valid_type && valid_block) {
        printf("LINE 156\n");
        // if file hasn't been allocated a data block yet, allocate one
        int data_block = 0;
        if(curr_inode->direct[0] == -1) {
            while(get_bit(data_bitmap, data_block) != 0)
                data_block++;
            set_bit(data_bitmap, data_block);
            // printf("chosen data block for file with inum %d: %d\n", m.inum, data_block);
        }

        // update file size in inode
        curr_inode->size = ((curr_inode->size) >= (m.offset + m.nbytes)) ? (curr_inode->size) : (m.offset + m.nbytes);

        // point inode direct[] to data block
        curr_inode->direct[m.offset/UFS_BLOCK_SIZE] = (s->data_region_addr + data_block) * UFS_BLOCK_SIZE;

        // write buffer to data block
        // if (pwrite(fd, &m.buffer, m.nbytes, curr_inode->direct[m.offset/UFS_BLOCK_SIZE] + m.offset) >= 0)
        printf("RIGHT BEFORE WRITE MEMCPY\n");
        printf("m.buffer: %s\n", m.write_buffer);
        printf("m.nbytes: %d\n", m.nbytes);
        char *write_ptr = image + (inode_table[m.inum].direct[m.offset/UFS_BLOCK_SIZE] * MFS_BLOCK_SIZE) + (m.offset % MFS_BLOCK_SIZE);
        memcpy(write_ptr, m.write_buffer, m.nbytes);
        printf("RIGHT AFTER WRITE MEMCPY\n");
        // printf("write_ptr: %s\n", write_ptr);
        ret = 0;
    }

    msync(&addr, image_size, MS_SYNC);
    fsync(fd);
    UDP_Write(sd, &addr, (char *) &ret, BUFFER_SIZE);
}

/*
    int MFS_Read(int inum, char *buffer, int offset, int nbytes): MFS_Read() reads nbytes of data (max size 4096 bytes)
    specified by the byte offset offset into the buffer from file specified by inum. The routine should work for either a
    file or directory; directories should return data in the format specified by MFS_DirEnt_t. Success: 0, failure: -1.
    Failure modes: invalid inum, invalid offset, invalid nbytes.
*/
void fsRead(msg m) {
    int ret = -1;

    inode_t *curr_inode = inode_table + (m.inum * sizeof(inode_t));
    int valid_inum = get_bit(inode_bitmap, m.inum) == 1;
    int valid_nbytes = m.nbytes >= 0 && m.nbytes <= 4096;
    int valid_offset = m.offset >= 0 && m.offset <= curr_inode->size;

    m.type = curr_inode->type;

    if(valid_inum && valid_nbytes && valid_offset) {
        switch(m.type) {
            case MFS_DIRECTORY:
                int parent_inum = -1;
                for(int i = 0; i < DIRECT_PTRS; i++) {
                    int block_num = curr_inode->direct[i];
                    dir_block_t *dir = (dir_block_t*)(image + (block_num * UFS_BLOCK_SIZE));
                    for (int j = 0; j < 128; j++) {
                        if((strcmp(dir->entries[j].name, "..") == 0) && dir->entries[j].inum != -1) {
                            parent_inum = dir->entries[j].inum;
                            break;
                        }
                    }
                }
                inode_t *parent_inode = inode_table + (parent_inum * sizeof(inode_t));
                char name[28];
                for(int i = 0; i < DIRECT_PTRS; i++) {
                    int block_num = parent_inode->direct[i];
                    dir_block_t *dir = (dir_block_t*)(image + (block_num * UFS_BLOCK_SIZE));
                    for (int j = 0; j < 128; j++) {
                        if(dir->entries[j].inum == m.inum) {
                            strcpy(name, dir->entries[j].name);
                            break;
                        }
                    }
                }
                strcpy(m.name, name);
                ret = 0;
                break;
            case MFS_REGULAR_FILE:
                // char buffer[BUFFER_SIZE];
                /**
                if (pread(fd, buffer, m.nbytes, curr_inode->direct[m.offset/UFS_BLOCK_SIZE] + m.offset) >= 0) {
                    memcpy(m.buffer, buffer, m.nbytes);
                    ret = 0;
                }
                */
                printf("RIGHT BEFORE READ MEMCPY\n");
                printf("m.nbytes: %d\n", m.nbytes);
                char *read_ptr = image + (inode_table[m.inum].direct[m.offset/UFS_BLOCK_SIZE] * MFS_BLOCK_SIZE) + (m.offset % MFS_BLOCK_SIZE);
                // printf("read_ptr: %s\n", read_ptr);
                memcpy(m.read_buffer, read_ptr, m.nbytes);
                printf("RIGHT AFTER READ MEMCPY\n");
                // printf("read_buffer: %s\n", m.read_buffer);
                ret = 0;
                break;
        }
    }

    m.read_return = ret;
    msync(&addr, image_size, MS_SYNC);
    fsync(fd);
    UDP_Write(sd, &addr, (char *) &m, sizeof(m));
}

int creatFile(msg m) {
    // check if pinum exists
    if(get_bit(inode_bitmap, m.pinum) == 0)
        return -1;

    // read inode bitmap to find free inode
    int new_inum = 0;
    while(get_bit(inode_bitmap, new_inum) != 0)
        new_inum++;
    
    // write inode bitmap to mark it allocated
    set_bit(inode_bitmap, new_inum);

    // write to new inode itself (to initialize)
    inode_t *new_inode = inode_table + (new_inum * sizeof(inode_t));
    new_inode->type = MFS_REGULAR_FILE;
    new_inode->size = 0;

    // write to data of directory (to link name of file to inode number, and update size)
    inode_t *parent_inode = inode_table + (m.pinum * sizeof(inode_t));
    dir_ent_t *dir = image + (parent_inode->direct[0] * UFS_BLOCK_SIZE);
    int i = 0;
    while(dir[i].inum != -1) {
        if(strcmp(m.name, dir[i].name) == 0) {
            return 0;
        }
        i++;
    }
    dir[i].inum = new_inum;
    strcpy(dir[i].name, m.name);
    parent_inode->size = parent_inode->size + sizeof(dir_ent_t);

    return 0;
}

int creatDir(msg m) {
    // check if pinum exists
    if(get_bit(inode_bitmap, m.pinum) == 0)
        return -1;
    
    // read inode bitmap to find free inode
    int new_inum = 0;
    while(get_bit(inode_bitmap, new_inum) != 0)
        new_inum++;
    
    // write inode bitmap to mark it allocated
    set_bit(inode_bitmap, new_inum);

    // write data bitmap to mark it allocated
    int data_block = 0;
    while(get_bit(data_bitmap, data_block) != 0)
        data_block++;
    set_bit(data_bitmap, data_block);
    
    // printf("chosen data block for new inum %d: %d\n", new_inum, data_block);

    // write to new inode itself (to initialize)
    inode_t *new_inode = inode_table + (new_inum * sizeof(inode_t));
    new_inode->type = MFS_DIRECTORY;
    new_inode->size = 2 * sizeof(dir_ent_t);
    new_inode->direct[0] = s->data_region_addr + data_block;
    for (int i = 1; i < DIRECT_PTRS; i++)
	    new_inode->direct[i] = -1;

    // write to data of parent directory (to link name of dir to inode number)
    inode_t *parent_inode = inode_table + (m.pinum * sizeof(inode_t));
    dir_ent_t *dir = image + (parent_inode->direct[0] * UFS_BLOCK_SIZE);
    int i = 0;
    while(dir[i].inum != -1) {
        if(strcmp(m.name, dir[i].name) == 0) {
            return 0;
        }
        i++;
    }
    dir[i].inum = new_inum;
    strcpy(dir[i].name, m.name);

    // write directory itself to chosen fre data block
    dir_block_t dir_block;
    strcpy(dir_block.entries[0].name, ".");
    dir_block.entries[0].inum = new_inum;

    strcpy(dir_block.entries[1].name, "..");
    dir_block.entries[1].inum = m.pinum;

    for (int i = 2; i < 128; i++)
	    dir_block.entries[i].inum = -1;
    
    if (pwrite(fd, &dir_block, UFS_BLOCK_SIZE, (s->data_region_addr + data_block) * UFS_BLOCK_SIZE) < 0)
        return -1;

    return 0;
}

/*
    int MFS_Creat(int pinum, int type, char *name): MFS_Creat() makes a file (type == MFS_REGULAR_FILE)
    or directory (type == MFS_DIRECTORY) in the parent directory specified by pinum of name name.
    Returns 0 on success, -1 on failure. Failure modes: pinum does not exist, or name is too long.
    If name already exists, return success.
*/
void fsCreat(msg m) {
    int ret = -1;
    inode_t *parent_inode = inode_table + (m.pinum * sizeof(inode_t));
    if(strlen(m.name) <= 28 && parent_inode->type == MFS_DIRECTORY) {
        switch(m.type) {
            case MFS_REGULAR_FILE:
                ret = creatFile(m);
                break;
            case MFS_DIRECTORY:
                ret = creatDir(m);
                break;
        }
    }
    fsync(fd);
    UDP_Write(sd, &addr, (char *) &ret, BUFFER_SIZE);
}

/*
    int MFS_Unlink(int pinum, char *name): MFS_Unlink() removes the file or directory name from the
    directory specified by pinum. 0 on success, -1 on failure. Failure modes: pinum does not exist,
    directory is NOT empty. Note that the name not existing is NOT a failure by our definition (think
    about why this might be).
*/
void fsUnlink(msg m) {
    int ret = -1;
    if (get_bit(inode_bitmap, m.pinum) == 1) {
        // get parent inode
        inode_t *parent_inode = inode_table + (m.pinum * sizeof(inode_t));

        // find the inum of name
        int inum;
        for(int i = 0; i < DIRECT_PTRS; i++) {
            int block_num = parent_inode->direct[i];
            if (block_num == -1) {
                break;
            }
            dir_block_t *dir = (dir_block_t*)(image + (block_num * UFS_BLOCK_SIZE));
            for (int j = 0; j < 128; j++) {
                if(dir->entries[j].inum != 0)
                if((strcmp(dir->entries[j].name, m.name) == 0) && dir->entries[j].inum != -1) {
                    inum = dir->entries[j].inum;
                    break;
                }
            }
        }

        // get the inode of inum
        inode_t *inode = inode_table + (inum * sizeof(inode_t));

        // check if file or directory
        if (inode->type == MFS_DIRECTORY) { // if it's a directory
            // check if it's empty
            if (inode->size == 2*sizeof(dir_ent_t)) {
                // reduce parent dir size by 1
                parent_inode->size -= sizeof(dir_ent_t);

                // set databitmap of file to 0
                set_bit_zero(data_bitmap, inum);

                // set inodebitmap of file to 0
                set_bit_zero(inode_bitmap, inum);

                // find parent dir entry
                dir_ent_t *parent_dir = image + (parent_inode->direct[0] * UFS_BLOCK_SIZE);

                // loop through entries in parent directory to locate current entry
                for (int i = 0; i < 128; i++) {
                    if (parent_dir[i].inum == inum) {
                        parent_dir[i].inum = -1;
                        ret = 0;
                        break;
                    }
                }
            }
        } else { // if it's a file
            // reduce parent dir size by 1
            parent_inode->size -= sizeof(dir_ent_t);

            // set databitmap of file to 0
            set_bit_zero(data_bitmap, inum);

            // set inodebitmap of file to 0
            set_bit_zero(inode_bitmap, inum);

            // find parent dir entry
            dir_ent_t *parent_dir = image + (parent_inode->direct[0] * UFS_BLOCK_SIZE);

            // loop through entries in parent directory to locate current entry
            for (int i = 0; i < 128; i++) {
                if (parent_dir[i].inum == inum) {
                    parent_dir[i].inum = -1;
                    ret = 0;
                    break;
                }
            }
        }
    }
    fsync(fd);
    UDP_Write(sd, &addr, (char *) &ret, BUFFER_SIZE);
}

/*
    int MFS_Shutdown(): MFS_Shutdown() just tells the server to force all of its data structures to disk
    and shutdown by calling exit(0). This interface will mostly be used for testing purposes.
*/
void fsShutdown(int fd) {
	fsync(fd);
	close(fd);
    UDP_Close(sd);
    munmap(image, image_size);
    exit(0);
}

int main(int argc, char *argv[]) {
	int port = atoi(argv[1]);
    sd = UDP_Open(port);
    assert(sd > -1);

	fd = open(argv[2], O_RDWR);
	assert(fd > -1);

    struct stat sbuf;
    int rc = fstat(fd, &sbuf);
    assert(rc > -1);

	image_size = (int) sbuf.st_size;
    image = mmap(NULL, image_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(image != MAP_FAILED);
	s = (super_t *) image;

    inode_table = (inode_t *) (image + (s->inode_region_addr * UFS_BLOCK_SIZE));
    root_inode = inode_table;
    root_dir = image + (root_inode->direct[0] * UFS_BLOCK_SIZE);
    inode_bitmap = image + (s->inode_bitmap_addr * UFS_BLOCK_SIZE);
    data_bitmap = image + (s->data_bitmap_addr * UFS_BLOCK_SIZE);

    printFSInfo();
	
    while (1) {
        msg m;

        printf("\nserver:: waiting...\n");
        int rc = UDP_Read(sd, &addr, (char *) &m, sizeof(msg));
        printf("server:: read message [call:(%s)]\n", calls[m.call]);
        
        if (rc > 0) {
            switch(m.call) {
                case LOOKUP:
                    fsLookup(m);
                    break;
                case STAT:
                    fsStat(m);
                    break;
                case WRITE:
                    fsWrite(m);
                    // printFSInfo();
                    break;
                case READ:
                    fsRead(m);
                    break;
                case CREAT:
                    fsCreat(m);
                    // printFSInfo();
                    break;
                case UNLINK:
                    fsUnlink(m);
                    // printFSInfo();
                    break;
                case SHUTDOWN:
                    fsShutdown(fd);
                    break;
            }

            printf("server:: reply\n");
        } 
    }

    return 0;
}
