CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2

# Executables à builder
TARGETS =  server_awale client_awale


OBJS = awale.o server_awale.o client_awale.o main.o

all: $(TARGETS)
	@rm -f $(OBJS) # delete .o files to not create clutter

# Le jeu awale 
awale: main.o awale.o
	$(CC) $(CFLAGS) -o $@ $^

# Le serveur
server_awale: server_awale.o awale.o
	$(CC) $(CFLAGS) -o $@ $^

# Le client 
client_awale: client_awale.o
	$(CC) $(CFLAGS) -o $@ $^

# La compilation ! 
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


#Pour nettoyer avec make clean 
clean:
	rm -f $(OBJS) $(TARGETS)

.PHONY: all clean
