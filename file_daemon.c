#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include "common.h"

#define MAX_FILES 100

/* --- FILE LOCK REGISTRY --- */
typedef struct {
    char filename[MAX_FILENAME];
    pthread_rwlock_t lock;
} FileRegistry;

FileRegistry registry[MAX_FILES];
int registry_count = 0;
pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    pthread_rwlock_t* new_lock = &registry[registry_count].lock;
    registry_count++;
    pthread_mutex_unlock(&registry_mutex);
    return new_lock;
}

/* --- THE DAEMON WORKER THREAD --- */
void* daemon_worker(void* arg) {
    Task* task = (Task*)arg;
    pthread_rwlock_t* file_lock = get_file_lock(task->filename);
    char cmd[2048];
    struct stat st;
    int fd, fd2;
    ssize_t bytes;
    char buffer[1024];

    printf("[Daemon] Processing OP %d for '%s' (Client: %d)\n", task->op, task->filename, task->client_pid);

    switch(task->op) {
        case OP_WRITE:
            pthread_rwlock_wrlock(file_lock);
            fd = open(task->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                write(fd, task->arg2, strlen(task->arg2));
                fsync(fd);
                close(fd);
                printf("[Success] Wrote to %s\n", task->filename);
            }
            pthread_rwlock_unlock(file_lock);
            break;
            
        case OP_READ:
            pthread_rwlock_rdlock(file_lock);
            fd = open(task->filename, O_RDONLY);
            if (fd >= 0) {
                printf("\n--- BEGIN %s ---\n", task->filename);
                while((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(STDOUT_FILENO, buffer, bytes);
                printf("\n--- END %s ---\n", task->filename);
                close(fd);
            } else perror("Read failed");
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_DELETE:
            pthread_rwlock_wrlock(file_lock);
            if (unlink(task->filename) == 0) printf("[Success] Deleted %s\n", task->filename);
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_META:
            pthread_rwlock_rdlock(file_lock); // Use 'file_lock' defined at line 41
            // Note: 'st' is already declared at line 44, so we just use it here
            if (stat(task->filename, &st) == 0) { // Use 'task' instead of 't'
                char time_buf[100];
                // Formatting the status change time
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&st.st_ctime));

                printf("\n--- Metadata for: %s ---\n", task->filename);
                printf("Size:        %ld bytes\n", (long)st.st_size);
                printf("Inode:       %ld\n", (long)st.st_ino);
                printf("Permissions: %o (Octal)\n", st.st_mode & 0777);
                printf("Status Date: %s\n", time_buf);
                printf("---------------------------\n");
            } else {
                printf("[Error] Metadata failed: %s not found.\n", task->filename);
            }
            pthread_rwlock_unlock(file_lock); // Use 'file_lock'
            break;

        case OP_COMPRESS:
            pthread_rwlock_rdlock(file_lock);
            snprintf(cmd, sizeof(cmd), "gzip -k -f %s", task->filename);
            if (system(cmd) == 0) printf("[Success] Compressed %s\n", task->filename);
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_DECOMPRESS:
            pthread_rwlock_wrlock(file_lock);
            snprintf(cmd, sizeof(cmd), "gunzip -k -f %s", task->filename);
            if (system(cmd) == 0) printf("[Success] Decompressed %s\n", task->filename);
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_COPY:
            pthread_rwlock_rdlock(file_lock);
            fd = open(task->filename, O_RDONLY);
            fd2 = open(task->arg2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0 && fd2 >= 0) {
                while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(fd2, buffer, bytes);
                close(fd); close(fd2);
                printf("[Success] Copied %s to %s\n", task->filename, task->arg2);
            }
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_RENAME:
            pthread_rwlock_wrlock(file_lock);
            if (rename(task->filename, task->arg2) == 0) printf("[Success] Renamed to %s\n", task->arg2);
            pthread_rwlock_unlock(file_lock);
            break;
    }
    
    free(task);
    return NULL;
}

int main() {
    int shm_id = shmget(SHM_KEY, sizeof(SharedQueue), IPC_CREAT | 0666);
    SharedQueue* queue = (SharedQueue*)shmat(shm_id, NULL, 0);

    // Initializing semaphores if this is a fresh start
    sem_init(&queue->mutex, 1, 1);
    sem_init(&queue->full, 1, 0);
    sem_init(&queue->empty, 1, MAX_TASKS);
    queue->head = 0; queue->tail = 0;

    printf("Daemon is LISTENING (SHM ID: %d)...\n", shm_id);

    while(1) {
        sem_wait(&queue->full);
        sem_wait(&queue->mutex);

        Task* new_task = malloc(sizeof(Task));
        *new_task = queue->tasks[queue->head];
        queue->head = (queue->head + 1) % MAX_TASKS;

        sem_post(&queue->mutex);
        sem_post(&queue->empty);

        pthread_t t;
        pthread_create(&t, NULL, daemon_worker, new_task);
        pthread_detach(t);
    }
    return 0;
}
