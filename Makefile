CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2

# Executables to build
TARGETS = awale server_awale client_awale

# Object files (built from corresponding .c files)
OBJS = awale.o server_awale.o client_awale.o main.o

all: $(TARGETS)

# awale (local terminal game) needs main.o + awale.o
awale: main.o awale.o
	$(CC) $(CFLAGS) -o $@ $^

# server needs server_awale.o + awale.o
server_awale: server_awale.o awale.o
	$(CC) $(CFLAGS) -o $@ $^

# client is standalone
client_awale: client_awale.o
	$(CC) $(CFLAGS) -o $@ $^

# Generic rule to compile .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGETS)

.PHONY: all clean
