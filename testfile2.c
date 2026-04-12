#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

/* --- CONSTANTS (Must match Daemon) --- */
typedef enum { 
    OP_READ, OP_WRITE, OP_DELETE, OP_RENAME, 
    OP_COPY, OP_META, OP_COMPRESS, OP_DECOMPRESS 
} OpType;

typedef struct {
    long mtype;
    int client_pid;
    int op;
    char filename[256];
    char data[1024];
} MsgPacket;

/* --- HELPER: Send Request to Queue --- */
void send_req(int qid, OpType op, const char* f1, const char* f2) {
    MsgPacket msg;
    msg.mtype = 1;
    msg.client_pid = getpid();
    msg.op = (int)op;
    
    strncpy(msg.filename, f1, 255);
    msg.filename[255] = '\0';
    
    if (f2) {
        strncpy(msg.data, f2, 1023);
        msg.data[1023] = '\0';
    } else {
        memset(msg.data, 0, 1024);
    }

    if (msgsnd(qid, &msg, sizeof(MsgPacket) - sizeof(long), 0) < 0) {
        perror("[-] msgsnd failed");
    } else {
        printf("[Client] Sent Op %d on %s\n", op, f1);
    }
}

int main() {
    key_t key = ftok(".", 'A');
    int qid = msgget(key, 0666);

    if (qid < 0) {
        perror("[-] Daemon not detected");
        exit(1);
    }

    printf("--- Initiating G24 Comprehensive Test Suite ---\n\n");

    // 1. Initial Creation
    send_req(qid, OP_WRITE, "master.txt", "File Management System Test Data.");
    sleep(1);

    // 2. Compression Cycle
    printf("\n[Test] Compressing master.txt...\n");
    send_req(qid, OP_COMPRESS, "master.txt", NULL);
    sleep(2); // Wait for gzip process

    // 3. Decompression Cycle 
    // Note: gzip creates 'master.txt.gz'. We tell the daemon to decompress it.
    printf("\n[Test] Decompressing master.txt.gz...\n");
    send_req(qid, OP_DECOMPRESS, "master.txt.gz", NULL);
    sleep(2);

    // 4. Verification Read
    printf("\n[Test] Reading master.txt after decompression cycle...\n");
    send_req(qid, OP_READ, "master.txt", NULL);
    sleep(2);

    // 5. Cleanup
    printf("\n[Test] Cleaning up environment...\n");
    send_req(qid, OP_DELETE, "master.txt", NULL);
    send_req(qid, OP_DELETE, "master.txt.gz", NULL);

    printf("\n--- Test Suite Dispatched ---\n");
    return 0;
}
