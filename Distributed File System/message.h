#ifndef __MESSAGE_h__
#define __MESSAGE_h__

#include "libmfs.h"

#define BUFFER_SIZE (4096)

#define INIT (0)
#define LOOKUP (1)
#define STAT (2)
#define WRITE (3)
#define READ (4)
#define CREAT (5)
#define UNLINK (6)
#define SHUTDOWN (7)

char calls[8][8] = {"INIT", "LOOKUP", "STAT", "WRITE", "READ", "CREAT", "UNLINK", "SHUTDOWN"};

typedef struct msg{
    int call;
    char hostname[BUFFER_SIZE];
    int port;
    int pinum;
    char name[BUFFER_SIZE];
    int inum;
    int stat_message_type;
    int stat_message_size;
    int stat_return;
    char read_buffer[BUFFER_SIZE];
    char write_buffer[BUFFER_SIZE];
    int offset;
    int nbytes;
    int type;
    int read_return;
} msg;

#endif // __MESSAGE_h__