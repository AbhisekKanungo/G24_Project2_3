#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>
/* --- GLOBAL SYNCHRONIZATION OBJECTS --- */
// Feature 1 & 2: RW Lock allows multiple concurrent readers OR one exclusive writer
pthread_rwlock_t file_rwlock; 
// Feature 8: Mutex ensures that log entries from different threads don't overlap
pthread_mutex_t log_mutex;

/* --- DATA STRUCTURES --- */
typedef enum { OP_READ, OP_WRITE, OP_DELETE, OP_RENAME, OP_COPY, OP_META, OP_COMPRESS, OP_DECOMPRESS } OpType;

/* --- REGISTRY & IPC STRUCTURES --- */

// The Dynamic Registry Node
typedef struct FileNode {
    char filename[256];
    pthread_rwlock_t rwlock;   // Fine-grained lock for THIS file only
    struct FileNode *next;
} FileNode;

// The Message Packet for IPC
typedef struct {
    long mtype;                // Required for msgrcv (can be used for priority)
    int client_pid;            // To identify the sender
    int op;                    // OpType (READ, WRITE, etc.)
    char filename[256];
    char data[1024];           // payload for WRITE or destination for COPY
} MsgPacket;

// Updated ThreadArgs for the worker
typedef struct {
    int thread_id;
    int op;
    char filename[256];
    char arg2[1024];
    pthread_rwlock_t *lock_ptr; // Pointer to the SPECIFIC lock in the registry
} ThreadArgs;
FileNode *registry_head = NULL;
pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_rwlock_t* get_file_lock(const char *fname) {
    pthread_mutex_lock(&registry_mutex);
    
    FileNode *curr = registry_head;
    while (curr) {
        if (strcmp(curr->filename, fname) == 0) {
            pthread_mutex_unlock(&registry_mutex);
            return &(curr->rwlock);
        }
        curr = curr->next;
    }

    // If not found, create a new Node (Dynamic Allocation)
    FileNode *new_node = malloc(sizeof(FileNode));
    strncpy(new_node->filename, fname, 255);
    pthread_rwlock_init(&(new_node->rwlock), NULL);
    new_node->next = registry_head;
    registry_head = new_node;

    pthread_mutex_unlock(&registry_mutex);
    return &(new_node->rwlock);
}

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
    char buffer[1024], log_msg[2048], cmd[512]; 
    ssize_t bytes;
    struct stat st;

    switch(args->op) {
	case OP_COMPRESS:
            snprintf(log_msg, sizeof(log_msg), "START: Compressing %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            // Use the DYNAMIC lock from the registry
            pthread_rwlock_rdlock(args->lock_ptr); 
            
            // -k (keep source) -f (force)
            snprintf(cmd, sizeof(cmd), "gzip -k -f %s 2>/dev/null", args->filename);
            
            if (system(cmd) == 0) 
                snprintf(log_msg, sizeof(log_msg), "DONE: Compression (SUCCESS)");
            else 
                snprintf(log_msg, sizeof(log_msg), "DONE: Compression (FAILED)");
            
            pthread_rwlock_unlock(args->lock_ptr);
            log_event(args->thread_id, log_msg);
            break;

        case OP_DECOMPRESS:
            snprintf(log_msg, sizeof(log_msg), "START: Decompressing %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            // Decompression is a destructive/modifying op, use Write Lock
            pthread_rwlock_wrlock(args->lock_ptr);
            
            snprintf(cmd, sizeof(cmd), "gunzip -k -f %s 2>/dev/null", args->filename);
            
            if (system(cmd) == 0) 
                snprintf(log_msg, sizeof(log_msg), "DONE: Decompression (SUCCESS)");
            else 
                snprintf(log_msg, sizeof(log_msg), "DONE: Decompression (FAILED)");
            
            pthread_rwlock_unlock(args->lock_ptr);
            log_event(args->thread_id, log_msg);
            break;
        case OP_WRITE:
            snprintf(log_msg, sizeof(log_msg), "START: Write to %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_wrlock(args->lock_ptr);
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
            pthread_rwlock_unlock(args->lock_ptr);
            log_event(args->thread_id, log_msg);
            break;

        case OP_READ:
            snprintf(log_msg, sizeof(log_msg), "START: Read of %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_rdlock(args->lock_ptr);
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
            pthread_rwlock_unlock(args->lock_ptr);
            log_event(args->thread_id, log_msg);
            break;
	case OP_DELETE:
    	    snprintf(log_msg, sizeof(log_msg), "START: Delete %s", args->filename);
    	    log_event(args->thread_id, log_msg);
    	    pthread_rwlock_wrlock(args->lock_ptr);
    	if (unlink(args->filename) == 0) 
        	snprintf(log_msg, sizeof(log_msg), "DONE: Delete %s (SUCCESS)", args->filename);
    	else 
     		snprintf(log_msg, sizeof(log_msg), "DONE: Delete %s (FAILED)", args->filename);
    	    pthread_rwlock_unlock(args->lock_ptr);
    	    log_event(args->thread_id, log_msg);
    	    break;
        case OP_META: // Feature 6: Metadata Display
            snprintf(log_msg, sizeof(log_msg), "START: Meta of %s", args->filename);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_rdlock(args->lock_ptr);
            if (stat(args->filename, &st) == 0) {
                printf("[Thread-%d Info] %s: Size=%ld bytes, Inode=%ld\n", args->thread_id, args->filename, st.st_size, st.st_ino);
                snprintf(log_msg, sizeof(log_msg), "DONE: Meta (SUCCESS)");
            } else snprintf(log_msg, sizeof(log_msg), "DONE: Meta (FAILED)");
            
            pthread_rwlock_unlock(args->lock_ptr);
            log_event(args->thread_id, log_msg);
            break;

        case OP_COPY: // Feature 5: File Copying
            snprintf(log_msg, sizeof(log_msg), "START: Copy %s to %s", args->filename, args->arg2);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_rdlock(args->lock_ptr);
            fd = open(args->filename, O_RDONLY);
            fd2 = open(args->arg2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0 && fd2 >= 0) {
                while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) write(fd2, buffer, bytes);
                snprintf(log_msg, sizeof(log_msg), "DONE: Copy (SUCCESS)");
            } else snprintf(log_msg, sizeof(log_msg), "DONE: Copy (FAILED)");
            
            if (fd >= 0) close(fd); if (fd2 >= 0) close(fd2);
            pthread_rwlock_unlock(args->lock_ptr);
            log_event(args->thread_id, log_msg);
            break;

        case OP_RENAME: // Feature 4: File Renaming
            snprintf(log_msg, sizeof(log_msg), "START: Rename %s to %s", args->filename, args->arg2);
            log_event(args->thread_id, log_msg);
            
            pthread_rwlock_wrlock(args->lock_ptr);
            if (rename(args->filename, args->arg2) == 0) 
                snprintf(log_msg, sizeof(log_msg), "DONE: Rename (SUCCESS)");
            else snprintf(log_msg, sizeof(log_msg), "DONE: Rename (FAILED)");
            
            pthread_rwlock_unlock(args->lock_ptr);
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
 /*   signal(SIGUSR1, handle_status_report);
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
    return 0;*/
    
    /* --- UPDATED MAIN EXECUTION --- */
    // Standard setup
    signal(SIGUSR1, handle_status_report);
    pthread_mutex_init(&log_mutex, NULL);
    // Note: No global file_rwlock needed anymore!

    // IPC Setup
    // Use a file path and project ID for ftok to be robust
    key_t key = ftok(".", 'A'); 
    int msgqid = msgget(key, 0666 | IPC_CREAT);
    
    if (msgqid < 0) {
        perror("msgget failed");
        return -1;
    }

    MsgPacket msgbuf;
    // msgsize is total struct size minus the long mtype header
    int msgsize = sizeof(MsgPacket) - sizeof(long);
    int thread_counter = 0;

    printf("--- Daemon Started: Listening on Message Queue (Key: %d) ---\n", key);

    while(1) {
        // msgrcv: (id, buffer, size, msg_type, flags)
        // type 0 means receive any message in the queue
        if (msgrcv(msgqid, &msgbuf, msgsize, 0, 0) < 0) {
            perror("msgrcv failed");
            continue; // Keep listening unless it's a fatal error
        }

        // 1. Get/Create the lock for THIS specific file (Dynamic Registry)
        pthread_rwlock_t *f_lock = get_file_lock(msgbuf.filename);

        // 2. Prepare Thread Arguments (Dependency Injection)
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->thread_id = ++thread_counter;
        args->op = msgbuf.op;
        strncpy(args->filename, msgbuf.filename, 255);
        strncpy(args->arg2, msgbuf.data, 1023);
        args->lock_ptr = f_lock; // Passing the specific registry lock

        // 3. Spawn Worker
        pthread_t t;
        if (pthread_create(&t, NULL, file_worker, args) != 0) {
            perror("Failed to create thread");
            free(args);
        } else {
            // Detach so we don't have to join. The thread cleans itself up.
            pthread_detach(t);
            log_event(0, "Dispatched worker thread for request.");
        }
    }

    // Cleanup (Usually reached via signal handler)
    msgctl(msgqid, IPC_RMID, NULL);
    return 0;
}
