
# Kernel Threads

In this project, I be added real kernel threads to xv6. Specifically, 
I do three things. First, I define a new system call
to create a kernel thread, called `clone()`, as well as one to wait for a
thread called `join()`. Then, I use `clone()` to build a little thread
library, with a `thread_create()` call and `lock_acquire()` and
`lock_release()` functions.

## Overview

The new clone system call looks like this: `int clone(void(*fcn)(void
*, void *), void *arg1, void *arg2, void *stack)`. This call creates a new
kernel thread which shares the calling process's address space. File
descriptors are copied as in `fork()`. The new process uses `stack` as its
user stack, which is passed two arguments (`arg1` and `arg2`) and uses a fake
return PC (`0xffffffff`); a proper thread will simply call `exit()` when it is
done (and not `return`). The stack is one page in size and
page-aligned. The new thread starts executing at the address specified by
`fcn`. As with `fork()`, the PID of the new thread is returned to the parent
(for simplicity, threads each have their own process ID).

The other new system call is `int join(void **stack)`. This call waits for a
child thread that shares the address space with the calling process to
exit. It returns the PID of waited-for child or -1 if none. The location of
the child's user stack is copied into the argument `stack` (which can then be
freed).

The thread library is built on top of this, and just has a simple `int
thread_create(void (*start_routine)(void *, void *), void *arg1, void *arg2)`
routine. This routine calls `malloc()` to create a new user stack, use
`clone()` to create the child thread and get it running. It returns the newly
created PID to the parent and 0 to the child (if successful), -1 otherwise.
An `int thread_join()` call is also created, which calls the underlying
`join()` system call, frees the user stack, and then returns. It returns the
waited-for PID (when successful), -1 otherwise.

The thread library also has a simple *ticket lock* (read [this book
chapter](http://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks.pdf) for more
information on this). There is a type `lock_t` that I use to declare
a lock, and two routines `void lock_acquire(lock_t *)` and `void
lock_release(lock_t *)`, which acquire and release the lock. The spin lock
uses x86 atomic add to build the lock -- see [this wikipedia
page](https://en.wikipedia.org/wiki/Fetch-and-add) for a way to create an
atomic fetch-and-add routine using the x86 `xaddl` instruction. One last
routine, `void lock_init(lock_t *)`, is used to initialize the lock as need be.

The thread library is available as part of every program that runs in
xv6. Thus, I added prototypes to `user/user.h` and the actual code to
implement the library routines in `user/ulib.c`.