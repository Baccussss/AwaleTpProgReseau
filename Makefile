CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2

SRCS = main.c awale.c
OBJS = $(SRCS:.c=.o)
TARGET = awale

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
