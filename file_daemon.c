#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <time.h>
#include "common.h"

#define MAX_FILES 100

typedef struct {
    char filename[MAX_FILENAME];
    pthread_rwlock_t lock;
} FileRegistry;

FileRegistry registry[MAX_FILES];
int registry_count = 0;

pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* NEW: Performance Stats */
long total_operations = 0;
double total_time = 0.0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_rwlock_t* get_file_lock(const char* filename) {
    pthread_mutex_lock(&registry_mutex);

    for (int i = 0; i < registry_count; i++) {
        if (strcmp(registry[i].filename, filename) == 0) {
            pthread_mutex_unlock(&registry_mutex);
            return &registry[i].lock;
        }
    }

    strcpy(registry[registry_count].filename, filename);
    pthread_rwlock_init(&registry[registry_count].lock, NULL);

    pthread_rwlock_t* lock = &registry[registry_count].lock;
    registry_count++;

    pthread_mutex_unlock(&registry_mutex);
    return lock;
}

/* Enhanced logging */
void log_action(int pid, const char* op, const char* file, const char* status, double time_taken) {
    pthread_mutex_lock(&log_mutex);

    FILE* f = fopen("system.log", "a");
    if (f) {
        time_t now = time(NULL);
        char tbuf[64];
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

        unsigned long tid = (unsigned long)pthread_self();

        fprintf(f, "[%s] TID:%lu PID:%d OP:%s FILE:%s TIME:%.2fms STATUS:%s\n",
                tbuf, tid, pid, op, file, time_taken, status);

        fclose(f);
    }

    pthread_mutex_unlock(&log_mutex);
}

void* daemon_worker(void* arg) {
    Task* task = (Task*)arg;
    pthread_rwlock_t* lock = get_file_lock(task->filename);

    char buffer[1024];
    int fd, fd2;
    ssize_t bytes;
    int success = 0;

    switch(task->op) {
        case OP_WRITE:
            pthread_rwlock_wrlock(lock);
            fd = open(task->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                write(fd, task->arg2, strlen(task->arg2));
                close(fd);
                success = 1;
            }
            pthread_rwlock_unlock(lock);
            break;

        case OP_READ:
            pthread_rwlock_rdlock(lock);
            fd = open(task->filename, O_RDONLY);
            if (fd >= 0) {
                while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
                    write(STDOUT_FILENO, buffer, bytes);
                close(fd);
                success = 1;
            }
            pthread_rwlock_unlock(lock);
            break;

        case OP_DELETE:
            pthread_rwlock_wrlock(lock);
            if (unlink(task->filename) == 0)
                success = 1;
            pthread_rwlock_unlock(lock);
            break;

        case OP_RENAME:
            pthread_rwlock_wrlock(lock);
            if (rename(task->filename, task->arg2) == 0)
                success = 1;
            pthread_rwlock_unlock(lock);
            break;

        case OP_COPY:
            pthread_rwlock_rdlock(lock);
            fd = open(task->filename, O_RDONLY);
            fd2 = open(task->arg2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0 && fd2 >= 0) {
                while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
                    write(fd2, buffer, bytes);
                close(fd); close(fd2);
                success = 1;
            }
            pthread_rwlock_unlock(lock);
            break;

        case OP_META: {
            struct stat st;
            pthread_rwlock_rdlock(lock);
            if (stat(task->filename, &st) == 0)
                success = 1;
            pthread_rwlock_unlock(lock);
            break;
        }

        case OP_COMPRESS: {
            char cmd[512];
            pthread_rwlock_rdlock(lock);
            snprintf(cmd, sizeof(cmd), "gzip -kf %s", task->filename);
            if (system(cmd) == 0)
                success = 1;
            pthread_rwlock_unlock(lock);
            break;
        }

        case OP_DECOMPRESS: {
            char cmd[512];
            pthread_rwlock_wrlock(lock);
            snprintf(cmd, sizeof(cmd), "gunzip -kf %s", task->filename);
            if (system(cmd) == 0)
                success = 1;
            pthread_rwlock_unlock(lock);
            break;
        }
    }

    /* Time calculation */
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);

    double time_taken =
        (end.tv_sec - task->start_time.tv_sec) * 1e3 +
        (end.tv_nsec - task->start_time.tv_nsec) / 1e6;

    const char* op_names[] = {
        "READ","WRITE","DELETE","RENAME",
        "COPY","META","COMPRESS","DECOMPRESS"
    };

    log_action(task->client_pid, op_names[task->op],
               task->filename,
               success ? "Success" : "Failed",
               time_taken);

    /* NEW: Update stats */
    pthread_mutex_lock(&stats_mutex);
    total_operations++;
    total_time += time_taken;

    if (total_operations % 10 == 0) {
        printf("\n📊 Performance Stats:\n");
        printf("Total Ops: %ld\n", total_operations);
        printf("Average Time: %.2f ms\n", total_time / total_operations);
    }
    pthread_mutex_unlock(&stats_mutex);

    free(task);
    return NULL;
}

int main() {
    int shm_id = shmget(SHM_KEY, sizeof(SharedQueue), IPC_CREAT | 0666);
    SharedQueue* q = shmat(shm_id, NULL, 0);

    sem_init(&q->mutex, 1, 1);
    sem_init(&q->full, 1, 0);
    sem_init(&q->empty, 1, MAX_TASKS);

    q->head = q->tail = 0;

    printf("Daemon running...\n");

    while (1) {
        sem_wait(&q->full);
        sem_wait(&q->mutex);

        Task* t = malloc(sizeof(Task));
        *t = q->tasks[q->head];
        q->head = (q->head + 1) % MAX_TASKS;

        sem_post(&q->mutex);
        sem_post(&q->empty);

        pthread_t th;
        pthread_create(&th, NULL, daemon_worker, t);
        pthread_detach(th);
    }
}