CC = gcc
CFLAGS = -g -Wall -std=c99

all: linkx20

linkx20: linkx20.o
	$(CC) linkx20.o -o linkx20

linkx20.o: linkx20.c
	$(CC) $(CFLAGS) -c linkx20.c 

clean:
	rm -f *.o linkx20 *.exe
