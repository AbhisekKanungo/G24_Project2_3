CC = gcc
CFLAGS = -Wall -pthread
TARGET = file_manager

all: $(TARGET)

$(TARGET): filemanager.c
	$(CC) filemanager.c -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET) system.log
