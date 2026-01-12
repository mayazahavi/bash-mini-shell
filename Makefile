CC = gcc
CFLAGS = -Wall -Wextra -std=c99

TARGET = mini_bash
SRC = mini_bash.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

