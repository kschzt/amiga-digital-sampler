CC=gcc
CFLAGS=-O3 -march=native -Wall
LIBS=-lasound -lm

all: sampler

sampler: main.o dsp.o
	$(CC) $(CFLAGS) -o sampler main.o dsp.o $(LIBS)

main.o: main.c dsp.h
	$(CC) $(CFLAGS) -c main.c

dsp.o: dsp.c dsp.h
	$(CC) $(CFLAGS) -c dsp.c

clean:
	rm -f *.o sampler
