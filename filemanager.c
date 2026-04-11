#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

// --- SYNCHRONIZATION: READER-WRITER LOCK ---
typedef struct {
    lock_t mutex;
    lock_t write_lock;
    int readers_count;
} rwlock_t;

void rwlock_init(rwlock_t *rw) {
    lock_init(&rw->mutex);
    lock_init(&rw->write_lock);
    rw->readers_count = 0;
}

void acquire_read(rwlock_t *rw) {
    lock_acquire(&rw->mutex);
    rw->readers_count++;
    if (rw->readers_count == 1) lock_acquire(&rw->write_lock);
    lock_release(&rw->mutex);
}

void release_read(rwlock_t *rw) {
    lock_acquire(&rw->mutex);
    rw->readers_count--;
    if (rw->readers_count == 0) lock_release(&rw->write_lock);
    lock_release(&rw->mutex);
}

void acquire_write(rwlock_t *rw) { lock_acquire(&rw->write_lock); }
void release_write(rwlock_t *rw) { lock_release(&rw->write_lock); }

// --- GLOBALS ---
rwlock_t file_lock;
lock_t console_lock; // Protects the terminal output

struct thread_args {
    char filename[64];
    char text[128];
};

void log_action(char *action, char *filename) {
    lock_acquire(&console_lock);
    printf(1, "[AUDIT LOG] Action: %s on File: %s\n", action, filename);
    lock_release(&console_lock);
}

// --- FEATURE 1: CONCURRENT READING ---
void read_file(void *arg1, void *arg2) {
    struct thread_args *args = (struct thread_args*)arg1;
    char buf[256];
    
    acquire_read(&file_lock);
    int fd = open(args->filename, O_RDONLY);
    if (fd >= 0) {
        int n = read(fd, buf, sizeof(buf)-1);
        if (n >= 0) buf[n] = '\0';
        
        lock_acquire(&console_lock);
        printf(1, "Read from %s: \n%s\n", args->filename, buf);
        lock_release(&console_lock);
        
        close(fd);
        log_action("READ", args->filename);
    }
    release_read(&file_lock);
    exit();
}

// --- FEATURE 2: EXCLUSIVE WRITING ---
void write_file(void *arg1, void *arg2) {
    struct thread_args *args = (struct thread_args*)arg1;
    
    acquire_write(&file_lock);
    int fd = open(args->filename, O_CREATE | O_WRONLY); 
    if (fd >= 0) {
        write(fd, args->text, strlen(args->text));
        close(fd);
        log_action("WRITE", args->filename);
    }
    release_write(&file_lock);
    exit();
}

// --- FEATURE 6: METADATA ---
void show_metadata(void *arg1, void *arg2) {
    struct thread_args *args = (struct thread_args*)arg1;
    struct stat st;
    
    acquire_read(&file_lock);
    if (stat(args->filename, &st) >= 0) {
        lock_acquire(&console_lock);
        printf(1, "Metadata for %s:\n", args->filename);
        printf(1, "  Type: %d (1=Dir, 2=File, 3=Dev)\n", st.type);
        printf(1, "  Inode: %d\n", st.ino);
        printf(1, "  Size: %d bytes\n", st.size);
        lock_release(&console_lock);
        
        log_action("METADATA", args->filename);
    }
    release_read(&file_lock);
    exit();
}

int main(int argc, char *argv[]) {
    rwlock_init(&file_lock);
    lock_init(&console_lock);
    
    printf(1, "Starting Interactive Multithreaded File Manager...\n");
    printf(1, "--------------------------------------------------\n");
    
    struct thread_args *args = (struct thread_args*)malloc(sizeof(struct thread_args));
    
    printf(1, "Enter the name of the file to create/write: ");
    gets(args->filename, 64);
    args->filename[strlen(args->filename)-1] = '\0';

    printf(1, "Enter the text you want to write into the file: ");
    gets(args->text, 128);

    printf(1, "\nExecuting Threads...\n");

    // Phase 1: Writer Thread
    thread_create(&write_file, (void*)args, 0);
    thread_join(); 

    // Phase 2: Concurrent Reader & Metadata Threads
    thread_create(&read_file, (void*)args, 0);
    thread_create(&show_metadata, (void*)args, 0);
    thread_join();
    thread_join();
    
    free(args);
    printf(1, "File operations complete. Exiting.\n");
    exit();
}
