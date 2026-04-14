CC = gcc
CFLAGS = -Wall -pthread
TARGETS = daemon client

all: $(TARGETS)

daemon: file_daemon.c common.h
	$(CC) file_daemon.c -o daemon $(CFLAGS)

client: client.c common.h
	$(CC) client.c -o client $(CFLAGS)

clean:
	rm -f daemon client system.log
