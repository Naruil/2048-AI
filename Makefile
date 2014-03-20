CC=gcc
CFLAGS=-g -O3 -lncurses

.PHONY : all
all: 2048.c 
	$(CC) 2048.c -o 2048 $(CFLAGS)

