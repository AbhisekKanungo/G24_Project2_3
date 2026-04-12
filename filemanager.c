#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

/* --- GLOBAL SYNCHRONIZATION OBJECTS --- */
// Feature 1 & 2: RW Lock allows multiple concurrent readers OR one exclusive writer
pthread_rwlock_t file_rwlock; 
// Feature 8: Mutex ensures that log entries from different threads don't overlap
pthread_mutex_t log_mutex;

/* --- DATA STRUCTURES --- */
typedef enum { OP_READ, OP_WRITE, OP_DELETE, OP_RENAME, OP_COPY, OP_META } OpType;

typedef struct {
    int thread_id;
    OpType op;
    char filename[256];
    char arg2[1024]; 
} ThreadArgs;

/* --- LOGGING SYSTEM (Feature 8: Auditing) --- */
void log_event(int t_id, const char *msg) {
    pthread_mutex_lock(&log_mutex);
    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strlen(ts) - 1] = '\0'; // Clean newline
    
    int log_fd = open("system.log", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (log_fd >= 0) {
        dprintf(log_fd, "[%s] Thread-%d: %s\n", ts, t_id, msg);
        close(log_fd);
    }
    printf("[%s] Thread-%d: %s\n", ts, t_id, msg);
    pthread_mutex_unlock(&log_mutex);
}

/* --- SIGNAL HANDLING (Note 1: Signals) --- */
void handle_status_report(int sig) {
    log_event(0, "SIGNAL RECEIVED - SIGUSR1: System Health Check Success.");
}

/* --- CORE WORKER FUNCTION --- */
void* file_worker(void* arguments) {
    ThreadArgs *args = (ThreadArgs*)arguments;
    int fd, fd2;
    char buffer[1024], log_msg[2048]; 
    ssize_t bytes;
    struct stat st;

    switch(args->op) {
        case OP_WRITE:
            snprintf(log_msg, sizeof(log_msg), "START: Write to %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_wrlock(&file_rwlock);
            fd = open(args->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                write(fd, args->arg2, strlen(args->arg2));
                
                // CRITICAL FIX: Force the kernel to flush the buffer to disk
                fsync(fd); 
                
                close(fd);
                snprintf(log_msg, sizeof(log_msg), "DONE: Write to %s (SUCCESS)", args->filename);
            } else {
                snprintf(log_msg, sizeof(log_msg), "DONE: Write (FAILED)");
            }
            sleep(1); 
            pthread_rwlock_unlock(&file_rwlock);
            log_event(args->thread_id, log_msg);
            break;

        case OP_READ:
            snprintf(log_msg, sizeof(log_msg), "START: Read of %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_rdlock(&file_rwlock);
            fd = open(args->filename, O_RDONLY);
            if (fd >= 0) {
                // Use a local buffer to avoid thread interference
                char local_buf[1024]; 
                ssize_t r_bytes;

                // Explicit header so we know who is starting
                dprintf(STDOUT_FILENO, "\n[Thread-%d] --- BEGIN READ ---\n", args->thread_id);

                // The Fix: Read and write the FULL file content
                while ((r_bytes = read(fd, local_buf, sizeof(local_buf))) > 0) {
                    // write() is thread-safe at the kernel level for STDOUT
                    write(STDOUT_FILENO, local_buf, r_bytes);
                }

                dprintf(STDOUT_FILENO, "\n[Thread-%d] --- END READ ---\n", args->thread_id);
                close(fd);
                snprintf(log_msg, sizeof(log_msg), "DONE: Read %s (SUCCESS)", args->filename);
            } else {
                snprintf(log_msg, sizeof(log_msg), "DONE: Read (FAILED)");
            }
            
            // This sleep ensures the threads overlap long enough for you to see it
            sleep(2); 
            pthread_rwlock_unlock(&file_rwlock);
            log_event(args->thread_id, log_msg);
            break;
	case OP_DELETE:
    	    snprintf(log_msg, sizeof(log_msg), "START: Delete %s", args->filename);
    	    log_event(args->thread_id, log_msg);
    	    pthread_rwlock_wrlock(&file_rwlock);
    	if (unlink(args->filename) == 0) 
        	snprintf(log_msg, sizeof(log_msg), "DONE: Delete %s (SUCCESS)", args->filename);
    	else 
     		snprintf(log_msg, sizeof(log_msg), "DONE: Delete %s (FAILED)", args->filename);
    	    pthread_rwlock_unlock(&file_rwlock);
    	    log_event(args->thread_id, log_msg);
    	    break;
        case OP_META: // Feature 6: Metadata Display
            snprintf(log_msg, sizeof(log_msg), "START: Meta of %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_rdlock(&file_rwlock);
            if (stat(args->filename, &st) == 0) {
                printf("[Thread-%d Info] %s: Size=%ld bytes, Inode=%ld\n", args->thread_id, args->filename, st.st_size, st.st_ino);
                snprintf(log_msg, sizeof(log_msg), "DONE: Meta (SUCCESS)");
            } else snprintf(log_msg, sizeof(log_msg), "DONE: Meta (FAILED)");
            
            pthread_rwlock_unlock(&file_rwlock);
            log_event(args->thread_id, log_msg);
            break;

        case OP_COPY: // Feature 5: File Copying
            snprintf(log_msg, sizeof(log_msg), "START: Copy %s to %s", args->filename, args->arg2);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_rdlock(&file_rwlock);
            fd = open(args->filename, O_RDONLY);
            fd2 = open(args->arg2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0 && fd2 >= 0) {
                while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(fd2, buffer, bytes);
                snprintf(log_msg, sizeof(log_msg), "DONE: Copy (SUCCESS)");
            } else snprintf(log_msg, sizeof(log_msg), "DONE: Copy (FAILED)");
            
            if (fd >= 0) close(fd); if (fd2 >= 0) close(fd2);
            pthread_rwlock_unlock(&file_rwlock);
            log_event(args->thread_id, log_msg);
            break;

        case OP_RENAME: // Feature 4: File Renaming
            snprintf(log_msg, sizeof(log_msg), "START: Rename %s to %s", args->filename, args->arg2);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_wrlock(&file_rwlock);
            if (rename(args->filename, args->arg2) == 0) 
                snprintf(log_msg, sizeof(log_msg), "DONE: Rename (SUCCESS)");
            else snprintf(log_msg, sizeof(log_msg), "DONE: Rename (FAILED)");
            
            pthread_rwlock_unlock(&file_rwlock);
            log_event(args->thread_id, log_msg);
            break;
    }
    free(arguments);
    return NULL;
}

/* --- HELPER: Spawn Thread --- */
void spawn(pthread_t *t, int id, OpType op, const char *f1, const char *f2) {
    ThreadArgs *a = malloc(sizeof(ThreadArgs));
    a->thread_id = id; a->op = op;
    
    strncpy(a->filename, f1, 255);
    a->filename[255] = '\0';
    
    if (f2) {
        strncpy(a->arg2, f2, 1023);
        a->arg2[1023] = '\0';
    }
    
    pthread_create(t, NULL, file_worker, a);
}

/* --- MAIN EXECUTION --- */
int main() {
    signal(SIGUSR1, handle_status_report);
    pthread_rwlock_init(&file_rwlock, NULL);
    pthread_mutex_init(&log_mutex, NULL);

    pthread_t t[10];
    printf("--- G24 Project 2: Multi-threaded System ---\n\n");

    const char *data = "Project: Multi-threaded File Manager\nInstitution: IIT (ISM) Dhanbad\nDetails: This code demonstrates Readers-Writer locking, Signal handling, and direct Linux system calls.\n";

    // 1. Initial Write
    spawn(&t[0], 1, OP_WRITE, "report.txt", data);
    pthread_join(t[0], NULL);

    // 2. Concurrent Ops (Read/Meta)
    spawn(&t[1], 2, OP_READ, "report.txt", NULL);
    spawn(&t[2], 3, OP_READ, "report.txt", NULL);
    spawn(&t[3], 4, OP_META, "report.txt", NULL);
    for(int i=1; i<4; i++) pthread_join(t[i], NULL);

    // 3. Management Ops
    spawn(&t[4], 5, OP_COPY, "report.txt", "backup.txt");
    pthread_join(t[4], NULL);

    spawn(&t[5], 6, OP_RENAME, "report.txt", "final_report.txt");
    pthread_join(t[5], NULL);

    // --- NEW: OPTIONAL DELETION STEP ---
    char choice;
    printf("\nBatch operations complete. Do you want to delete the generated files? (y/n): ");
    scanf(" %c", &choice);

    if (choice == 'y' || choice == 'Y') {
        printf("Cleaning up files...\n");
        // Deleting the files created/renamed during the process
        spawn(&t[6], 7, OP_DELETE, "backup.txt", NULL);
        spawn(&t[7], 8, OP_DELETE, "final_report.txt", NULL);
        
        pthread_join(t[6], NULL);
        pthread_join(t[7], NULL);
    } else {
        printf("Cleanup skipped. Files 'backup.txt' and 'final_report.txt' preserved.\n");
    }

    pthread_rwlock_destroy(&file_rwlock);
    pthread_mutex_destroy(&log_mutex);
    
    printf("\nExecution Finished. Audit available in system.log\n");
    return 0;
}
