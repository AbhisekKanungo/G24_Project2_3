# Multithreaded File Management System (Linux Daemon)

A high-performance, multithreaded file management daemon designed for Linux. This project implements a **Client-Server Architecture** using **System V Message Queues** for Inter-Process Communication (IPC) and a **Dynamic File Registry** for fine-grained concurrency control.

## 🚀 Overview
Originally inspired by kernel-level implementations in xv6, this project transitions those concepts into a robust Linux user-space service. It allows multiple independent processes to request file operations simultaneously while maintaining strict data integrity through advanced synchronization.

## 🏗️ Core Architecture
- **Event-Driven Daemon:** A background process that listens on a message queue and dispatches tasks to worker threads.
- **Dynamic File Registry:** An in-memory linked list that tracks every active file. Each file entry contains its own `pthread_rwlock_t`, enabling **Fine-Grained Locking** [locking specific files rather than the whole system].
- **Reader-Writer Pattern:** Supports concurrent **OP_READ** and **OP_META** operations, while providing **Mutual Exclusion** [exclusive access] for **OP_WRITE**, **OP_DELETE**, and **OP_RENAME**.

## 🛠️ Technical Stack
| Feature | Technology |
| :--- | :--- |
| **IPC** | System V Message Queues (`ftok`, `msgget`, `msgrcv`) |
| **Threading** | POSIX Threads (`libpthread`) with detached execution |
| **Sync** | `pthread_rwlock_t` (Per-file) and `pthread_mutex_t` (Global Log) |
| **I/O** | Linux System Calls (`open`, `read`, `write`, `fsync`, `stat`, `unlink`) |

## 📂 File Structure
- `filemanager.c`: The Daemon (Server) source code.
- `testfile.c`: Comprehensive test suite (Client) to simulate high-concurrency load.
- `system.log`: Automatically generated audit trail documenting every thread action.

## ⚡ Getting Started

### 1. Compilation
Link the `pthread` library during compilation:
```bash
# Compile the Daemon
gcc -o daemon filemanager.c -lpthread

# Compile the Test Client
gcc -o test_suite testfile.c
