CC=gcc
CFLAGS=-g -pedantic -std=gnu17 -Wall -Wextra

TARGET = nyush

SRC = Shell.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)