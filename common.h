#ifndef COMMON_H
#define COMMON_H

#include <semaphore.h>
#include <time.h>

#define SHM_KEY 0x1234
#define MAX_TASKS 50
#define MAX_FILENAME 256
#define MAX_DATA 1024

typedef enum { 
    OP_READ, OP_WRITE, OP_DELETE, OP_RENAME, 
    OP_COPY, OP_META, OP_COMPRESS, OP_DECOMPRESS 
} OpType;

typedef struct {
    int client_pid;
    OpType op;
    char filename[MAX_FILENAME];
    char arg2[MAX_DATA];

    struct timespec start_time;

} Task;

typedef struct {
    Task tasks[MAX_TASKS];
    int head;
    int tail;

    sem_t mutex;
    sem_t full;
    sem_t empty;
} SharedQueue;

#endif