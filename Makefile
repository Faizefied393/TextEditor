CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99

kilo: kilo.c
	$(CC) $(CFLAGS) -o kilo kilo.c

clean:
	rm -f kilo

