# Distributed File System

In this assignment, I developed a working distributed file server. 
I was provided with only the bare minimal UDP communication code; I built the rest.

## Description

My file server is built as a stand-alone UDP-based server. It waits
for a message and then processes the message as need be, replying to the given
client.

My file server stores all of its data in an on-disk, fixed-sized
file which is referred to as the *file system image*. This image
contains the on-disk representation of your data structures; I
 use these system calls to access it: `open(), read(), write(),
lseek(), close(), fsync().`

To access the file server, I also built a client library. The
interface that the library supports is defined in [mfs.h](mfs.h).  The
library is called `libmfs.so`, and any programs that wish to access
the file server will link with it and call its various routines.

A simple UDP client and server:
- `client.c`: client code, sends a message to the server and waits for a reply
- `server.c`: server code, waits for messages indefinitely and replies

Both use `udp.c` as a simple UDP communication library.

The `Makefile` builds `client` and `server` executables. Type `make` to do this.

To run: type `server &` to run the server in the background; then type `client` to
run the client. You will likely then want to kill the server if you are done.

If you want to run these on different machines, you'll have to change the client
to send messages to the machine the server is running upon, instead of `localhost`.

## How to run the code

IN ONE TERMINAL:
```
make clean
make
LD_LIBRARY_PATH=*path/to/current/directory*
export LD_LIBRARY_PATH
./server 10000 test.img
```

IN ANOTHER TERMINAL:
```
LD_LIBRARY_PATH=*path/to/current/directory*
export LD_LIBRARY_PATH
./client
```