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

/* --- FILE LOCK REGISTRY --- */
typedef struct {
    char filename[MAX_FILENAME];
    pthread_rwlock_t lock;
} FileRegistry;

FileRegistry registry[MAX_FILES];
int registry_count = 0;
pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global lock specifically for the audit log to prevent thread race conditions
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* --- THREAD-SAFE AUDIT LOGGER --- */
void log_action(int client_pid, const char* op_name, const char* filename, const char* status) {
    pthread_mutex_lock(&log_mutex); // Lock the file
    
    FILE *log_file = fopen("system.log", "a");
    if (log_file != NULL) {
        time_t now;
        time(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        pthread_t tid = pthread_self();

        fprintf(log_file, "[%s] Thread ID: %lu | Client PID: %d | OP: %-8s | File: %-12s | Status: %s\n", 
                time_str, (unsigned long)tid, client_pid, op_name, filename, status);
        
        fclose(log_file);
    }
    
    pthread_mutex_unlock(&log_mutex); // Unlock for the next thread
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
        case OP_WRITE://Feature 1
            pthread_rwlock_wrlock(file_lock);
            fd = open(task->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                write(fd, task->arg2, strlen(task->arg2));
                fsync(fd);
                close(fd);
                printf("[Success] Wrote to %s\n", task->filename);
                log_action(task->client_pid, "WRITE", task->filename, "Success");
            } else {
                log_action(task->client_pid, "WRITE", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;
            
        case OP_READ://Feature 0
            pthread_rwlock_rdlock(file_lock);
            fd = open(task->filename, O_RDONLY);
            if (fd >= 0) {
                printf("\n--- BEGIN %s ---\n", task->filename);
                while((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(STDOUT_FILENO, buffer, bytes);
                printf("\n--- END %s ---\n", task->filename);
                close(fd);
                log_action(task->client_pid, "READ", task->filename, "Success");
            } else {
                perror("Read failed");
                log_action(task->client_pid, "READ", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_DELETE://Feature 2
            pthread_rwlock_wrlock(file_lock);
            if (unlink(task->filename) == 0) {
                printf("[Success] Deleted %s\n", task->filename);
                log_action(task->client_pid, "DELETE", task->filename, "Success");
            } else {
                log_action(task->client_pid, "DELETE", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_META://Feature 5
            pthread_rwlock_rdlock(file_lock);
            if (stat(task->filename, &st) == 0) { 
                char time_buf[100];
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&st.st_ctime));

                printf("\n--- Metadata for: %s ---\n", task->filename);
                printf("Size:        %ld bytes\n", (long)st.st_size);
                printf("Inode:       %ld\n", (long)st.st_ino);
                printf("Permissions: %o (Octal)\n", st.st_mode & 0777);
                printf("Status Date: %s\n", time_buf);
                printf("---------------------------\n");
                log_action(task->client_pid, "META", task->filename, "Success");
            } else {
                printf("[Error] Metadata failed: %s not found.\n", task->filename);
                log_action(task->client_pid, "META", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock); 
            break;

        case OP_COMPRESS://Feature 6
            pthread_rwlock_rdlock(file_lock);
            snprintf(cmd, sizeof(cmd), "gzip -k -f %s", task->filename);
            if (system(cmd) == 0) {
                printf("[Success] Compressed %s\n", task->filename);
                log_action(task->client_pid, "COMPRESS", task->filename, "Success");
            } else {
                log_action(task->client_pid, "COMPRESS", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_DECOMPRESS://Feature 7
            pthread_rwlock_wrlock(file_lock);
            snprintf(cmd, sizeof(cmd), "gunzip -k -f %s", task->filename);
            if (system(cmd) == 0) {
                printf("[Success] Decompressed %s\n", task->filename);
                log_action(task->client_pid, "DECOMPRESS", task->filename, "Success");
            } else {
                log_action(task->client_pid, "DECOMPRESS", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_COPY://Feature 4
            pthread_rwlock_rdlock(file_lock);
            fd = open(task->filename, O_RDONLY);
            fd2 = open(task->arg2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0 && fd2 >= 0) {
                while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(fd2, buffer, bytes);
                close(fd); close(fd2);
                printf("[Success] Copied %s to %s\n", task->filename, task->arg2);
                log_action(task->client_pid, "COPY", task->filename, "Success");
            } else {
                if (fd >= 0) close(fd);
                if (fd2 >= 0) close(fd2);
                log_action(task->client_pid, "COPY", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;

        case OP_RENAME://Feature 3
            pthread_rwlock_wrlock(file_lock);
            if (rename(task->filename, task->arg2) == 0) {
                printf("[Success] Renamed to %s\n", task->arg2);
                log_action(task->client_pid, "RENAME", task->filename, "Success");
            } else {
                log_action(task->client_pid, "RENAME", task->filename, "Failed");
            }
            pthread_rwlock_unlock(file_lock);
            break;
    }
    
    free(task);
    return NULL;
}

int main() {
    int shm_id = shmget(SHM_KEY, sizeof(SharedQueue), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("[Fatal] shmget failed");
        exit(1);
    }

    SharedQueue* queue = (SharedQueue*)shmat(shm_id, NULL, 0);
    if (queue == (void*)-1) {
        perror("[Fatal] shmat failed");
        exit(1);
    }

    // Initializing semaphores
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
