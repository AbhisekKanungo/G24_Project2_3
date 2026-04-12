#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

/* --- CONSTANTS (Must match Daemon) --- */
typedef enum { OP_READ, OP_WRITE, OP_DELETE, OP_RENAME, OP_COPY, OP_META } OpType;

typedef struct {
    long mtype;
    int client_pid;
    int op;
    char filename[256];
    char data[1024];
} MsgPacket;

void send_request(int qid, OpType op, const char* f1, const char* f2) {
    MsgPacket msg;
    msg.mtype = 1; // Standard Priority
    msg.client_pid = getpid();
    msg.op = op;
    strncpy(msg.filename, f1, 255);
    if (f2) strncpy(msg.data, f2, 1023);
    else memset(msg.data, 0, 1024);

    if (msgsnd(qid, &msg, sizeof(MsgPacket) - sizeof(long), 0) < 0) {
        perror("msgsnd failed");
    } else {
        printf("[Client] Sent Op %d on %s\n", op, f1);
    }
}

int main() {
    key_t key = ftok(".", 'A');
    int qid = msgget(key, 0666);

    if (qid < 0) {
        perror("Daemon not found. Run ./daemon first");
        exit(1);
    }

    printf("--- Initiating Comprehensive Test Suite ---\n");

    // Case 1: Create a base file
    printf("\n1. Testing OP_WRITE...\n");
    send_request(qid, OP_WRITE, "master.txt", "Initial content from automated test suite.");
    sleep(2); // Give daemon time to process

    // Case 2: Concurrent Reads (Stress Test)
    // In the daemon terminal, you should see these overlapping
    printf("\n2. Testing Concurrent OP_READ (Fine-grained locking check)...\n");
    for(int i = 0; i < 3; i++) {
        send_request(qid, OP_READ, "master.txt", NULL);
    }
    sleep(3);

    // Case 3: Metadata Check
    printf("\n3. Testing OP_META...\n");
    send_request(qid, OP_META, "master.txt", NULL);
    sleep(1);

    // Case 4: File Copying
    printf("\n4. Testing OP_COPY...\n");
    send_request(qid, OP_COPY, "master.txt", "copy_of_master.txt");
    sleep(2);

    // Case 5: Renaming
    printf("\n5. Testing OP_RENAME...\n");
    send_request(qid, OP_RENAME, "copy_of_master.txt", "final_test.txt");
    sleep(2);

    // Case 6: Final Cleanup check
    printf("\n6. Testing OP_DELETE...\n");
    send_request(qid, OP_DELETE, "master.txt", NULL);
    
    printf("\n--- Test Suite Dispatched ---\nCheck daemon terminal and system.log for results.\n");

    return 0;
}
