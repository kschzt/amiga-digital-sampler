CC=gcc
CC=gcc
CFLAGS=-O3 -march=native -Wall
LIBS=-lasound -lm -lpthread -lgpiod

OBJS = main.o dsp.o audio.o spi.o ringbuf.o presets.o ui.o gpio_monitor.o

sampler: $(OBJS)
	$(CC) $(CFLAGS) -o sampler $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o sampler
