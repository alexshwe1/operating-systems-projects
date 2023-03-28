
# Parallel Sort Project

Sorting, or alphabetizing as we called it as a child, is still a
critical task for data-intensive applications, including databases,
spreadsheets, and many other data-oriented applications. In this
project, I built a high-performance parallel sort. 

There were three specific objectives to this assignment:

* To familiarize with the Linux pthreads.
* To learn how to parallelize a program.
* To learn how to program for high performance.


## Project Specification

Parallel sort (`psort`) takes two command-line arguments.

```
prompt> ./psort input output
```

The input file consists of records; within each record is a
key. The key is the first four bytes of the record. The records are
fixed-size, and are each 100 bytes (which includes the key).

A successful sort reads all the records into memory from the input
file, sorts them, and then writes them out to the output file.
