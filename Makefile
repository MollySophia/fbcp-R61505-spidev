CFLAGS=-c -Wall -O3 -std=c11 -D_POSIX_C_SOURCE=199309L
LIBS= -lm

all: fbcp

fbcp: Makefile main.o r61505_spi.o
	$(CC) main.o r61505_spi.o $(LIBS) -o fbcp

test: Makefile test.o r61505_spi.o
	$(CC) test.o r61505_spi.o $(LIBS) -o test

main.o: main.c r61505_spi.o
	$(CC) $(CFLAGS) main.c

test.o: test.c r61505_spi.o
	$(CC) $(CFLAGS) test.c

r61505_spi.o: r61505_spi.c
	$(CC) $(CFLAGS) r61505_spi.c

clean:
	rm *.o fbcp test


