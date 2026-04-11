# Multithreaded File Management System in xv6

## Overview
This project implements a multithreaded file management system in user space for the xv6 operating system. It focuses on safe concurrency using a custom synchronization architecture. It is built on top of the joeylemon/xv6-threads repository, utilizing the underlying clone system calls and lock_t spinlocks.

## Features Implemented
- **Concurrent File Reading:** Multiple threads can read a file simultaneously using a Reader-Writer lock without blocking each other.
- **Exclusive File Writing:** Ensures only one thread can write at a time, preventing data corruption.
- **File Metadata Extraction:** Displays file size, inode number, and type using the xv6 stat() system call.
- **Thread-Safe Auditing:** Uses a console_lock to ensure logs and outputs do not overlap.
- **Interactive Input:** Accepts filenames and text input from the user via the xv6 shell.

## Files Created & Modified
- **filemanager.c:** Core program containing Reader-Writer lock (rwlock_t), thread functions, and main execution logic.
- **Makefile:** Added _filemanager to UPROGS and updated compiler settings.
- **README:** Created a dummy file to satisfy mkfs dependency for fs.img generation.

## How to Build and Run
1. Compile: 'make clean && make qemu'
2. Run inside xv6: '$ filemanager'
