# Makefile for bash-mini shell
# System Programming – HW3

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic
TARGET  = bash-mini
SRC     = bash_mini.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
