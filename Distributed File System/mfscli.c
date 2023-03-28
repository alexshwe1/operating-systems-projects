// Version 3
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "libmfs.h"
#include "ufs.h"

#define MFS_RW_BUFFER_SIZE 4096
#define LOG_SIZE 4096

char logBuffer[LOG_SIZE];
int verboseMode = 0;
void VERBOSE() {
    if (verboseMode != 1) return;
    printf("[VERBOSE] %s\n", logBuffer);
}

void INFO() {
    printf("[INFO] %s\n", logBuffer);
}

void ERR() {
    printf("[ERR] %s\n", logBuffer);
    exit(-1);
}

int _connect(char *hostname, int port) {
    sprintf(logBuffer, "Attemping to connect to %s:%d", hostname, port); INFO();
    int rc = MFS_Init(hostname, port);
    if (rc != 0) {
        sprintf("MFS_Init failed for %s:%d", hostname, port); ERR();
    }
    return rc;
}

// assumes absolute path.
// exits on traversal failure
int _traverseToDirectory(char *path) {
    assert(strlen(path) > 0);
    assert(path[0] == '/');

    path = strdup(path); // because strtok is destructive
    char *dirname = strtok(path, "/");
    
    // root directory is inode 0
    int dirInode = 0;
    while (dirname != NULL) {
        if (strcmp(dirname, "") == 0) { 
            dirname = strtok(NULL, "/");
            continue; // to handle // and trailing /
        }

        sprintf(logBuffer, "looking up child entry %s in parent directory (inode=%d)", dirname, dirInode); VERBOSE();
        dirInode = MFS_Lookup(dirInode, dirname);
        sprintf(logBuffer, "Found child entry with inode number %d", dirInode); VERBOSE();
        
        if (dirInode == -1) {
            sprintf(logBuffer, "Unable to enter %s", dirname); ERR();
            exit(1);
        };
        dirname = strtok(NULL, "/");
    }
    free(path);
    return dirInode;
}

int rfind(const char *haystack, char needle) {
    int end = strlen(haystack) - 1;
    for(int i = end; i >= 0; i--) {
        if (haystack[i] == needle) return i;
    }
    return -1;
}

// can only be called on directories.
int perform_ls(char *path) {
    int dirInode = _traverseToDirectory(path);

    MFS_Stat_t stat;
    int rc = MFS_Stat(dirInode, &stat);
    if (rc == -1) {
        sprintf(logBuffer, "Unable to call MFS_Stat on dir (inum=%d)", dirInode); ERR();
    }
    if (stat.type != UFS_DIRECTORY) {
        sprintf(logBuffer, "The inode (%d) received for %s is not of directory type", dirInode, path); ERR();
    }

    int sz = stat.size;
    assert(sz % sizeof(MFS_DirEnt_t) == 0);
    int nentries = sz / sizeof(MFS_DirEnt_t);
    MFS_DirEnt_t entries[nentries]; // should be safe to pass to MFS_Read because struct seems packed. No padding should be necessary.

    assert(sizeof(entries) == (28+4)*nentries); // folks, students, if this assert fails, email me!
    // Future me: the solution would be to read in X bytes specifically, and explicitly force-read entries by casting at required offsets.

    sprintf(logBuffer, "Attempting to read %d children of %s", nentries, path); VERBOSE();

    int offset = 0;
    while (offset < sz) {
        int toRead = sz - offset;
        if (toRead > MFS_RW_BUFFER_SIZE) toRead = MFS_RW_BUFFER_SIZE;
        rc = MFS_Read(dirInode, entries, offset, toRead);
        if (rc == -1) {
            sprintf(logBuffer, "MFS_Read failed"); ERR();
        }
        offset += toRead;
    }

    sprintf(logBuffer, "Fetched %d children. Here they are!", nentries); INFO();
    for(int i = 0; i < nentries; i++) {
        if (entries[i].inum < 0)
            printf("Skipping entry: (inode=%d)", entries[i].inum);
        else
            printf("%s (inode=%d)\n", entries[i].name, entries[i].inum);
    }
    return 0;
}

int perform_insert(const char *fromPath, char *toPath) {
    assert(strlen(toPath) > 0 && strlen(fromPath) > 0);

    int toCopyFd = open(fromPath, O_RDONLY);
    if (toCopyFd == -1) {
        sprintf(logBuffer, "Unable to open provided file %s", fromPath); ERR();
    }

    // assumes toPath ends with filename to copy as
    int fnameSep = rfind(toPath, '/');
    char *dirPath = strndup(toPath, fnameSep);
    char *fileName = toPath + fnameSep + 1;

    int dirInode = _traverseToDirectory(dirPath);

    sprintf(logBuffer, "Trying to create new file %s in %s", fileName, dirPath); VERBOSE();

    int rc = MFS_Creat(dirInode, UFS_REGULAR_FILE, fileName);
    if (rc == -1) {
        sprintf(logBuffer, "Unable to create new file %s in %s", fileName, dirPath); ERR();
    }

    int newInode = MFS_Lookup(dirInode, fileName);
    if (newInode == -1) {
        sprintf(logBuffer, "Unable to fetch newly created inode number even though MFS_Creat was successful"); ERR();
    }

    sprintf(logBuffer, "Created new file with inode number %d", newInode); INFO();

    char buffer[MFS_RW_BUFFER_SIZE];
    memset(buffer, 0, MFS_RW_BUFFER_SIZE);

    int readBytes = read(toCopyFd, buffer, MFS_RW_BUFFER_SIZE);
    int offset = 0;
    while (readBytes > 0) {
        sprintf(logBuffer, "about to write %d bytes ", readBytes); VERBOSE();
        
        int rc = MFS_Write(newInode, buffer, offset, readBytes);
        offset += readBytes;

        if (rc == -1) {
            sprintf(logBuffer, "MFS_Write failed"); ERR();
        }
        sprintf(logBuffer, "Written %d bytes successfully", readBytes); VERBOSE();
        readBytes = read(toCopyFd, buffer, MFS_RW_BUFFER_SIZE);
    }
    if (readBytes == -1) {
        sprintf(logBuffer, "Error while reading input file"); ERR();
    }

    sprintf(logBuffer, "Completed all write operations. Written a total of %d bytes", offset); INFO();

    free(dirPath);
    return 0;
}

int perform_cat(char *path) {
    int fnameSep = rfind(path, '/');
    char *dirPath = strndup(path, fnameSep);
    char *fileName = path + fnameSep + 1;

    int dirInode = _traverseToDirectory(dirPath);

    int fileInode = MFS_Lookup(dirInode, fileName);
    if (fileInode == -1) {
        sprintf(logBuffer, "Unable to lookup file %s in directory (inum=%d)", fileName, dirInode); ERR();
    }

    sprintf(logBuffer, "Trying to determine filesize"); VERBOSE();

    MFS_Stat_t stat;
    int rc = MFS_Stat(fileInode, &stat);
    if (rc == -1) {
        sprintf(logBuffer, "Unable to determine filesize. Stat failed for inum=%d", fileInode); ERR();
    }

    int sz = stat.size;
    char *output = (char *) malloc(sz * sizeof(char));
    memset(output, 0, sz);
    
    sprintf(logBuffer, "Filesize=%d. Starting read", sz); INFO();

    int offset = 0;
    while (offset < sz) {
        int count = sz - offset;
        if (count > MFS_RW_BUFFER_SIZE) count = MFS_RW_BUFFER_SIZE;

        sprintf(logBuffer, "Trying to read %d bytes from offset %d foi inum=%d", count, offset, fileInode); VERBOSE();
        int rc = MFS_Read(fileInode, output + offset, offset, count);
        if (rc == -1) {
            sprintf(logBuffer, "MFS_Read failed for inum=%d offset=%d count=%d", fileInode, offset, count); ERR();
        }

        offset += count;
    }

    sprintf(logBuffer, "File contents (from next line): \n%s\n", output);
    INFO();

    free(output);
    free(dirPath);
    return 0;
}

// similar to mkdir -p. Just bulldoze through and call MFS_Creat for all
// subdirectories. If name already exists, should not overwrite.
int perform_mkdir(char *path) {
    assert(strlen(path) > 0);
    assert(path[0] == '/');

    path = strdup(path); // because strtok is destructive.
    char *dirname = strtok(path, "/");
    
    // root directory is inode 0
    int dirInode = 0;
    while (dirname != NULL) { // assume root directory already exists. Creating further ones.
        if (strcmp(dirname, "") == 0) {
            dirname = strtok(NULL, "/");
            continue; // to handle // and trailing /
        }

        sprintf(logBuffer, "calling MFS_Creat for %s in parent directory (inode=%d)", dirname, dirInode); VERBOSE();
        int rc = MFS_Creat(dirInode, UFS_DIRECTORY, dirname);
        if (rc == -1) {
            sprintf(logBuffer, "Unable to create directory %s", dirname); ERR();
        }

        int newInode = MFS_Lookup(dirInode, dirname);
        if (newInode == -1) {
            sprintf(logBuffer, "Unable to fetch newly created directory inode even though MFS_Creat was successful"); ERR();
        }

        dirInode = newInode;
        
        if (dirInode == -1) {
            sprintf(logBuffer, "Unable to enter %s", dirname); ERR();
            exit(1);
        };
        dirname = strtok(NULL, "/");
    }
    sprintf(logBuffer, "mkdir completed successfully"); INFO();
    free(path);
    return 0;
}

const char *usage =  "mfscli usage: \n"
    "Basic format: ./mfscli ip_of_server port <command> <args...>\n"
    "              If the server is on the same machine, use 127.0.0.1 as ip\n"
    "\n"
    "Verbose mode: you can run all commands of mfscli in verbose mode by \n"
    "       prepending MFS_VERBOSE=1.\n"
    "       for e.g. MFS_VERBOSE=1 ./mfscli 127.0.0.1 36000 ls /files/\n\n"
    "Usage:\n"
    " - ./mfscli 127.0.0.1 36000 insert /path/to/local/file/test.txt /files/test1.txt \n"
    "       This copies the file specified by first path into MFS with \n"
    "       the location specified by the second path.\n"
    "       First path refers to a file in your original filesystem (AFS) \n"
    "       Second path refers to a location in MFS.\n"
    "       The directory should exist in MFS for insert to succeed. \n"
    "\n"
    " - ./mfscli 127.0.0.1 36000 cat /files/test1.txt \n"
    "       similar to UNIX cat. Outputs content of /files/test1.txt. Issues \n"
    "       corresponding MSF_Read, MFS_Lookup, MFS_Stat calls for this. \n"
    "       Fails if file/path does not exist. \n"
    "\n"
    " - ./mfscli 127.0.0.1 36000 ls /files/ \n"
    "       Similar to UNIX ls. The path argument is for a location within MFS.\n"
    "       It should end with a directory. doing /files/test1.txt is not \n"
    "       supported.\n"
    "\n"
    " - ./mfscli 127.0.0.1 36000 mkdir /files/new/directory \n"
    "       This works similar to unix's mkdir -p. Basically it calls MFS_Creat \n"
    "       for each subdirectory. First MFS_Creat(files), then MFS_Creat(new) within \n"
    "       it and so on. Existing directories would ideally remain untouched \n"
    "       because MFS_Creat doesn't do anything and returns true for existing dirs\n"
    "\n"
    ;

int _assert_argc(int argc, int expected) {
    if (argc != expected) {
        printf("Incorrect number of arguments! Run ./mfscli for usage help\n");
        exit(0);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    memset(logBuffer, 0, LOG_SIZE);

    // TODO: move to argparse
    if (argc <= 3) {  // bare minumum: ./mfscli host port
        printf("%s", usage);
        return -1;
    }

    char *verboseEnv = getenv("MFS_VERBOSE");
    if (verboseEnv != NULL && strcmp(verboseEnv, "1") == 0) 
        verboseMode = 1;

    _connect(argv[1], atoi(argv[2]));

    char *cmd = argv[3];
    if (strcmp(cmd, "insert") == 0) {
        _assert_argc(argc, 3 + 3);
        perform_insert(argv[4], argv[5]);
    } else if (strcmp(cmd, "cat") == 0) {
        _assert_argc(argc, 2 + 3);
        perform_cat(argv[4]);
    } else if (strcmp(cmd, "ls") == 0) {
        _assert_argc(argc, 2 + 3);
        perform_ls(argv[4]);
    } else if (strcmp(cmd, "mkdir") == 0) {
        _assert_argc(argc, 2 + 3);
        perform_mkdir(argv[4]);
    } else {
        printf("Command not found! run ./mfscli for usage help\n");
        return -1;
    }

    return 0;
}