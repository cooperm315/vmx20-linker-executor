CC = gcc
CFLAGS = -g -Wall -std=c99

all: driver

vmx20.o: vmx20.c
	$(CC) $(CFLAGS) -c vmx20.c

vmx20: vmx20.o
	ar rcs libvmx20.a vmx20.o

driver.o: driver.c
	$(CC) $(CFLAGS) -c driver.c

driver: driver.o vmx20
	gcc -o driver driver.o -L. -lvmx20

clean: 
	rm -f libvmx20.a *.o driver
