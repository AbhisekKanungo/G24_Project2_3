#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

// --- Synchronization Locks ---
// rwlock: Allows multiple concurrent readers, but STRICTLY ONE exclusive writer
pthread_rwlock_t file_rwlock; 
pthread_mutex_t log_mutex;    // Ensures logs don't overwrite each other in the terminal

// --- Operation Codes ---
typedef enum {
    OP_READ, OP_WRITE, OP_DELETE, OP_RENAME, 
    OP_COPY, OP_META, OP_COMPRESS, OP_DECOMPRESS
} OpType;

// --- Thread Argument Structure ---
typedef struct {
    int thread_id;
    OpType op;
    char filename[128];
    char arg2[128]; // Used for text payload, new name, or destination file
} ThreadArgs;

// --- Thread-Safe Logger ---
void log_action(int t_id, const char *action, const char *filename, const char *status) {
    pthread_mutex_lock(&log_mutex);
    time_t now;
    time(&now);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; // Remove newline
    
    FILE *log_file = fopen("system.log", "a");
    if(log_file) {
        fprintf(log_file, "[%s] Thread-%d | %s on '%s' | Status: %s\n", time_str, t_id, action, filename, status);
        fclose(log_file);
    }
    printf("[%s] Thread-%d | %s on '%s' | Status: %s\n", time_str, t_id, action, filename, status);
    pthread_mutex_unlock(&log_mutex);
}

// --- The Worker Thread ---
void* file_worker(void* arguments) {
    ThreadArgs *args = (ThreadArgs*)arguments;
    char cmd[256];
    struct stat file_stat;

    switch(args->op) {
        case OP_WRITE:
            // EXCLUSIVE WRITE: Wait until all readers/writers are done, then lock
            pthread_rwlock_wrlock(&file_rwlock); 
            FILE *f_write = fopen(args->filename, "a");
            if (f_write) {
                fprintf(f_write, "%s\n", args->arg2);
                fclose(f_write);
                log_action(args->thread_id, "WRITE", args->filename, "SUCCESS");
            } else {
                log_action(args->thread_id, "WRITE", args->filename, "FAILED (Cannot open)");
            }
            sleep(1); // Simulate work to prove exclusivity
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_READ:
            // CONCURRENT READ: Multiple threads can hold this lock simultaneously
            pthread_rwlock_rdlock(&file_rwlock);
            FILE *f_read = fopen(args->filename, "r");
            if (f_read) {
                char buffer[256];
                printf("[Thread-%d reads]: ", args->thread_id);
                while(fgets(buffer, sizeof(buffer), f_read)) {
                    printf("%s", buffer);
                }
                fclose(f_read);
                log_action(args->thread_id, "READ", args->filename, "SUCCESS");
            } else {
                log_action(args->thread_id, "READ", args->filename, "FAILED");
            }
            sleep(1); // Simulate work to prove concurrency
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_META:
            pthread_rwlock_rdlock(&file_rwlock);
            if(stat(args->filename, &file_stat) == 0) {
                printf("[Thread-%d META]: Size=%ld bytes, Permissions=%o\n", 
                        args->thread_id, file_stat.st_size, file_stat.st_mode & 0777);
                log_action(args->thread_id, "METADATA", args->filename, "SUCCESS");
            } else {
                log_action(args->thread_id, "METADATA", args->filename, "FAILED");
            }
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_COPY:
            pthread_rwlock_rdlock(&file_rwlock); // Lock source for reading
            FILE *src = fopen(args->filename, "r");
            FILE *dst = fopen(args->arg2, "w");
            if(src && dst) {
                char ch;
                while((ch = fgetc(src)) != EOF) fputc(ch, dst);
                log_action(args->thread_id, "COPY", args->filename, "SUCCESS");
            } else {
                log_action(args->thread_id, "COPY", args->filename, "FAILED");
            }
            if(src) fclose(src);
            if(dst) fclose(dst);
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_RENAME:
            pthread_rwlock_wrlock(&file_rwlock); // Lock file exclusively to rename
            if(rename(args->filename, args->arg2) == 0) {
                log_action(args->thread_id, "RENAME", args->filename, "SUCCESS");
            } else {
                log_action(args->thread_id, "RENAME", args->filename, "FAILED");
            }
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_COMPRESS:
            pthread_rwlock_wrlock(&file_rwlock);
            snprintf(cmd, sizeof(cmd), "gzip -f %s", args->filename);
            if(system(cmd) == 0) log_action(args->thread_id, "COMPRESS", args->filename, "SUCCESS");
            else log_action(args->thread_id, "COMPRESS", args->filename, "FAILED");
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_DECOMPRESS:
            pthread_rwlock_wrlock(&file_rwlock);
            snprintf(cmd, sizeof(cmd), "gzip -d -f %s.gz", args->filename);
            if(system(cmd) == 0) log_action(args->thread_id, "DECOMPRESS", args->filename, "SUCCESS");
            else log_action(args->thread_id, "DECOMPRESS", args->filename, "FAILED");
            pthread_rwlock_unlock(&file_rwlock);
            break;

        case OP_DELETE:
            pthread_rwlock_wrlock(&file_rwlock);
            if(remove(args->filename) == 0) {
                log_action(args->thread_id, "DELETE", args->filename, "SUCCESS");
            } else {
                log_action(args->thread_id, "DELETE", args->filename, "FAILED");
            }
            pthread_rwlock_unlock(&file_rwlock);
            break;
    }
    
    free(arguments);
    return NULL;
}

// --- Helper to spawn threads ---
void create_thread(pthread_t *t, int id, OpType op, const char *f1, const char *f2) {
    ThreadArgs *args = malloc(sizeof(ThreadArgs));
    args->thread_id = id;
    args->op = op;
    strcpy(args->filename, f1);
    if(f2) strcpy(args->arg2, f2);
    pthread_create(t, NULL, file_worker, (void*)args);
}

int main() {
    // Initialize Synchronization Locks
    pthread_rwlock_init(&file_rwlock, NULL);
    pthread_mutex_init(&log_mutex, NULL);

    pthread_t threads[10];

    printf("--- Multi-Threaded File Management System ---\n\n");

    // TEST 1: Exclusive Write
    create_thread(&threads[0], 1, OP_WRITE, "target.txt", "Initial file data.");
    pthread_join(threads[0], NULL); // Wait for setup

    // TEST 2: Concurrent Readers (These will run at the exact same time)
    create_thread(&threads[1], 2, OP_READ, "target.txt", NULL);
    create_thread(&threads[2], 3, OP_READ, "target.txt", NULL);
    create_thread(&threads[3], 4, OP_READ, "target.txt", NULL);
    
    // TEST 3: Metadata and Copy
    create_thread(&threads[4], 5, OP_META, "target.txt", NULL);
    create_thread(&threads[5], 6, OP_COPY, "target.txt", "backup.txt");

    // Wait for readers/metadata to finish before proceeding to destructive ops
    for(int i = 1; i <= 5; i++) pthread_join(threads[i], NULL);

    // TEST 4: Compress and Decompress
    create_thread(&threads[6], 7, OP_COMPRESS, "target.txt", NULL);
    pthread_join(threads[6], NULL);
    
    create_thread(&threads[7], 8, OP_DECOMPRESS, "target.txt", NULL);
    pthread_join(threads[7], NULL);

    // TEST 5: Rename and Delete
    create_thread(&threads[8], 9, OP_RENAME, "target.txt", "old_target.txt");
    pthread_join(threads[8], NULL);

    create_thread(&threads[9], 10, OP_DELETE, "old_target.txt", NULL);
    pthread_join(threads[9], NULL);

    // Cleanup
    pthread_rwlock_destroy(&file_rwlock);
    pthread_mutex_destroy(&log_mutex);

    printf("\n--- Simulation Complete. Check system.log for history. ---\n");
    return 0;
}
