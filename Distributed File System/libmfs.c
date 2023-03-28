#include <stdio.h>
#include <ctype.h>
#include "libmfs.h"
#include "udp.h"
#include "message.h"

#define BUFFER_SIZE (4096)

int sd = -1;
int rc;
struct sockaddr_in saddr;

int MFS_Init(char *hostname, int port) {
    sd = UDP_Open(20000);
	assert(sd > -1);
	rc = UDP_FillSockAddr(&saddr, hostname, port);
	assert(rc == 0);
	return 0;
}

int MFS_Lookup(int pinum, char *name) {
    // instantiate message struct
    struct msg m;
    m.call = LOOKUP;
    m.pinum = pinum;
    strcpy(m.name, name);

    // send message to server through UDP
    rc = UDP_Write(sd, &saddr, (char *) &m, sizeof(struct msg));

    // timer setup
    fd_set r;
    FD_ZERO(&r);
    FD_SET(sd, &r);
    struct timeval t;
    t.tv_sec = 5;
    t.tv_usec = 0;
    rc = select(sd + 1, &r, NULL, NULL, &t);

    int ret;

	if(rc < 0) {
		return -1;
	}	
	else if (rc == 0) {
		return MFS_Lookup(pinum, name);
	}
	else {
		// struct sockaddr_in raddr;
		rc = UDP_Read(sd, &saddr, (char *) &ret, sizeof(ret));
        if(rc < 0)
            return -1;
	}

	return ret;
}

int MFS_Stat(int inum, MFS_Stat_t *m) {
    // instantiate message struct
    struct msg msg;
    msg.call = STAT;
    msg.inum = inum;

    // send message to server through UDP
    rc = UDP_Write(sd, &saddr, (char *) &msg, sizeof(struct msg));

    // timer setup
    fd_set r;
    FD_ZERO(&r);
    FD_SET(sd, &r);
    struct timeval t;
    t.tv_sec = 5;
    t.tv_usec = 0;
    rc = select(sd + 1, &r, NULL, NULL, &t);

	if(rc < 0) {
		return -1;
	}	
	else if (rc == 0) {
		return MFS_Stat(inum, m);
	}
	else {
		// struct sockaddr_in raddr;
		rc = UDP_Read(sd, &saddr, (char *) &msg, sizeof(struct msg));
        m->size = msg.stat_message_size;
        m->type = msg.stat_message_type;
	}
	return msg.stat_return;
}

int MFS_Write(int inum, char *buffer, int offset, int nbytes) {
    // instantiate message struct
    struct msg m;
    m.call = WRITE;
    m.inum = inum;
    memcpy(m.write_buffer, buffer, nbytes);
    m.offset = offset;
    m.nbytes = nbytes;

    // send message to server through UDP
    rc = UDP_Write(sd, &saddr, (char *) &m, sizeof(struct msg));

    // timer setup
    fd_set r;
    FD_ZERO(&r);
    FD_SET(sd, &r);
    struct timeval t;
    t.tv_sec = 5;
    t.tv_usec = 0;
    rc = select(sd + 1, &r, NULL, NULL, &t);

	if(rc < 0) {
		return -1;
	}	
	else if (rc == 0) {
		return MFS_Write(inum, buffer, offset, nbytes);
	}
	else {
		// struct sockaddr_in raddr;
		rc = UDP_Read(sd, &saddr, (char *) &m, sizeof(struct msg));
	}

	return 0;
}

int MFS_Read(int inum, char *buffer, int offset, int nbytes) {
    // instantiate message struct
    struct msg m;
    m.call = READ;
    m.inum = inum;
    memcpy(m.read_buffer, buffer, nbytes);
    m.offset = offset;
    m.nbytes = nbytes;

    // send message to server through UDP
    rc = UDP_Write(sd, &saddr, (char *) &m, sizeof(struct msg));

    // timer setup
    fd_set r;
    FD_ZERO(&r);
    FD_SET(sd, &r);
    struct timeval t;
    t.tv_sec = 5;
    t.tv_usec = 0;
    rc = select(sd + 1, &r, NULL, NULL, &t);

	if(rc < 0) {
		return -1;
	}	
	else if (rc == 0) {
		return MFS_Read(inum, buffer, offset, nbytes);
	}
	else {
		// struct sockaddr_in raddr;
		rc = UDP_Read(sd, &saddr, (char *) &m, sizeof(struct msg));
	}

    if(m.type == MFS_DIRECTORY) {
        MFS_DirEnt_t dirent;
        dirent.inum = inum;
        strcpy(dirent.name, m.name);
        memcpy(buffer, (char *) &dirent, sizeof(dirent));
    } else if(m.type == MFS_REGULAR_FILE) {
        printf("nbytes: %d\n", nbytes);
        memcpy(buffer, (char *) m.read_buffer, nbytes);
        printf("buffer: %s\n", buffer);
        printf("size of read_buffer: %ld\n", strlen(buffer));
    }

	return m.read_return;
}

int MFS_Creat(int pinum, int type, char *name) {
    // instantiate message struct
    struct msg m;
    m.call = CREAT;
    m.pinum = pinum;
    m.type = type;
    strcpy(m.name, name);

    // send message to server through UDP
    rc = UDP_Write(sd, &saddr, (char *) &m, sizeof(struct msg));

    // timer setup
    fd_set r;
    FD_ZERO(&r);
    FD_SET(sd, &r);
    struct timeval t;
    t.tv_sec = 5;
    t.tv_usec = 0;
    rc = select(sd + 1, &r, NULL, NULL, &t);

    int ret;

	if(rc < 0) {
		return -1;
	}	
	else if (rc == 0) {
		return MFS_Creat(pinum, type, name);
	}
	else {
		rc = UDP_Read(sd, &saddr, (char *) &ret, sizeof(ret));
        if(rc < 0)
            return -1;
	}

	return ret;
}

int MFS_Unlink(int pinum, char *name) {
    // instantiate message struct
    struct msg m;
    m.call = UNLINK;
    m.pinum = pinum;
    strcpy(m.name, name);

    // send message to server through UDP
    rc = UDP_Write(sd, &saddr, (char *) &m, sizeof(struct msg));

    // timer setup
    fd_set r;
    FD_ZERO(&r);
    FD_SET(sd, &r);
    struct timeval t;
    t.tv_sec = 5;
    t.tv_usec = 0;
    rc = select(sd + 1, &r, NULL, NULL, &t);

    int ret;

	if(rc < 0) {
		return -1;
	}	
	else if (rc == 0) {
		return MFS_Unlink(pinum, name);
	}
	else {
		// struct sockaddr_in raddr;
		rc = UDP_Read(sd, &saddr, (char *) &ret, sizeof(struct msg));
	}

	return ret;
}

int MFS_Shutdown() {
	if (sd < 0) {
		return sd;
	} 
	struct msg m;
    m.call = SHUTDOWN;
    rc = UDP_Write(sd, &saddr, (char *) &m, sizeof(struct msg));
	UDP_Close(sd);
	return 0;
}