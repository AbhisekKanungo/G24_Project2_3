# Multi-threaded File Management System

## Overview
An advanced multi-threaded file manager implemented in C using POSIX threads. The system ensures high-performance logical concurrency for reads while maintaining strict physical exclusivity for writes and archival operations.

## Implemented Features
* **Compression & Decompression:** Integrated user-optional archival tools using `gzip` and `gunzip` system calls.
* **RW-Lock Concurrency:** Uses `pthread_rwlock` to facilitate multiple simultaneous readers without blocking.
* **Management Suite:** Fully implemented `DELETE`, `RENAME`, `COPY`, and `META` (Inode/Size) operations.
* **Asynchronous Status Monitoring:** Custom `SIGUSR1` handler for real-time health checks.
* **Data Integrity:** Integrated `fsync()` calls to ensure kernel-level persistence.
* **Thread-Safe Auditing:** Centralized `system.log` protected by mutex synchronization.

## Execution Guide
### Compilation
```bash
gcc filemanager.c -o file_manager -pthread
```

### Running the System
```bash
./file_manager
```

## Technical Fixes
* **Archival Logic:** Added logic for optional, user-prompted compression and decompression steps during execution.
* **Buffer Stability:** Expanded `ThreadArgs` buffer to **1024 bytes** to resolve string truncation issues.
* **Race Condition Mitigation:** Resolved terminal output conflicts using mutex-guarded logging.
