# Multithreaded File Management System

A high-performance, multithreaded file management service designed for Linux. This project implements a Client-Server Architecture using Shared Memory (SHM) for Inter-Process Communication and a Dynamic File Registry for fine-grained concurrency control.

## Overview
Originally inspired by kernel-level implementations in xv6, this project transitions those concepts into a robust Linux user-space service. It allows multiple independent processes to request file operations simultaneously while maintaining data integrity through synchronization.

## Core Architecture
- **Shared-Memory Daemon:** A background process that monitors a shared memory queue and dispatches tasks to worker threads using POSIX Semaphores.
- **Dynamic File Registry:** An in-memory registry that tracks active files. Each entry contains a `pthread_rwlock_t`, enabling Fine-Grained Locking.
- **Reader-Writer Pattern:** Supports concurrent OP_READ, OP_META, and OP_COMPRESS operations, while enforcing Mutual Exclusion for OP_WRITE, OP_DELETE, OP_RENAME, and OP_DECOMPRESS.

## Supported Features
- **Read / Write:** High-throughput concurrent reading and safe, exclusive writing.
- **File Management:** Copy, rename, and delete files without race conditions.
- **Metadata Retrieval:** Instantly fetch file size, inode, and permission statistics.
- **Compression / Decompression:** Native integration with system utilities to shrink and expand files (.gz) directly via client requests.

## Technical Stack
| Feature | Technology |
| :--- | :--- |
| **IPC** | System V Shared Memory (`shmget`, `shmat`) + POSIX Semaphores |
| **Threading** | POSIX Threads (`libpthread`) with detached execution |
| **Sync** | `pthread_rwlock_t` (Per-file) and `sem_t` (IPC protection) |
| **I/O** | Linux System Calls (`open`, `read`, `write`, `fsync`, `stat`, `unlink`) |

## Getting Started
1. **Compile:** `make`
2. **Run Daemon:** `./daemon`
3. **Run Client:** `./client`
