# Multi-Threaded File Management System

A concurrent file management system implemented in C using POSIX Threads (**pthreads**) on Linux.

## Features
* **Concurrent Reading:** Multiple threads can read simultaneously using `pthread_rwlock_t`.
* **Exclusive Writing:** Ensures only one writer has access at a time.
* **Signal Handling:** Implements `SIGUSR1` for real-time system status reports.
* **Robust Error Handling:** Validates file descriptors and system call returns.
* **File Operations:** Supports Copy, Rename, Delete, Metadata, and Compression (gzip).

## Synchronization Mechanisms
* **RW Locks:** Used for the Readers-Writer problem to maximize concurrency.
* **Mutexes:** Used to protect the shared log file/terminal output.

## How to Run
1. **Compile:**
   ```bash
   make
   ```
2. **Execute:**
   ```bash
   ./file_manager
   ```
