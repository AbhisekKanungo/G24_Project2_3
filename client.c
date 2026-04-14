#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "common.h"

void clear_input() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int main() {
    int id = shmget(SHM_KEY, sizeof(SharedQueue), 0666);
    if (id < 0) {
        perror("Daemon not running. Start ./daemon first");
        return 1;
    }
    SharedQueue* q = (SharedQueue*)shmat(id, NULL, 0);

    // Create per-PID response FIFO
   char fifo_path[64];
   get_fifo_path(getpid(), fifo_path, sizeof(fifo_path));
   mkfifo(fifo_path, 0666);

    int choice;
    char f1[MAX_FILENAME], f2[MAX_DATA];

    while (1) {
        printf("\n--- File Manager IPC Shell ---\n");
        printf("0: READ    1: WRITE   2: DELETE   3: RENAME\n");
        printf("4: COPY    5: META    6: COMPRESS 7: DECOMPRESS\n");
        printf("-1: EXIT\n");
        printf("Selection > ");
        
        if (scanf("%d", &choice) != 1) {
            clear_input();
            continue;
        }
        if (choice == -1) break;

        // Collect required info based on choice
        printf("Enter filename: ");
        scanf("%s", f1);
        
        f2[0] = '\0'; // Reset arg2
        if (choice == 1) { // WRITE
            printf("Enter text to append: ");
            clear_input();
            fgets(f2, MAX_DATA, stdin);
            f2[strcspn(f2, "\n")] = 0; // Remove newline
        } else if (choice == 3 || choice == 4) { // RENAME or COPY
            printf("Enter target filename: ");
            scanf("%s", f2);
        }

        // Dispatch to Shared Memory
        Task t;
        t.client_pid = getpid();
        t.op = (OpType)choice;
        strncpy(t.filename, f1, MAX_FILENAME - 1);
        strncpy(t.arg2, f2, MAX_DATA - 1);

        sem_wait(&q->empty);
        sem_wait(&q->mutex);
        q->tasks[q->tail] = t;
        q->tail = (q->tail + 1) % MAX_TASKS;
        sem_post(&q->mutex);
        sem_post(&q->full);

        // printf("\n[✓] Sent Op %d to Daemon. Check daemon terminal for results.\n", choice);

    // Wait for daemon's response

    printf("[~] Waiting for daemon response...\n");
    int fd = open(fifo_path, O_RDONLY);
    if (fd < 0) {
    perror("Could not open response FIFO");
    continue;
   }
   Response resp;
   ssize_t n = read(fd, &resp, sizeof(Response));
   close(fd);

   if (n == sizeof(Response)) {
    if (resp.success)
        printf("[✓] Success: %s\n", resp.message);
    else
        printf("[✗] Failed:  %s\n", resp.message);
    if (resp.data[0] != '\0')
        printf("%s\n", resp.data);
    } else {
    printf("[!] Malformed response from daemon.\n");
    }
    }
     
    unlink(fifo_path); // clean up FIFO on exit
    shmdt(q);
    printf("Goodbye!\n");
    return 0;
}
