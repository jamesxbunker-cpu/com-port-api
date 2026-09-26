CC     = gcc
AR     = ar
CFLAGS = -Wall -Wextra -O2 -Iinc
LIBS   = -lkernel32

all: readcom.exe comstreamd.exe client.exe ringtest.exe libcomstream.a example.exe

readcom.exe: src/main.c src/serial.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

comstreamd.exe: src/daemon.c src/serial.c src/ringbuf.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

client.exe: src/client.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

ringtest.exe: src/ringtest.c src/ringbuf.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

libcomstream.a: src/comstream.o
	$(AR) rcs $@ $^

src/comstream.o: src/comstream.c inc/comstream.h
	$(CC) $(CFLAGS) -c $< -o $@

example.exe: src/example_consumer.c libcomstream.a
	$(CC) $(CFLAGS) -o $@ src/example_consumer.c libcomstream.a $(LIBS)

clean:
	rm -f *.exe *.o *.a src/*.o

.PHONY: all clean