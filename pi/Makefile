CC=gcc
CFLAGS=-O3 -march=native -Wall -MMD -MP
LIBS=-lasound -lm -lpthread -lgpiod

OBJS = main.o dsp.o audio.o spi.o ringbuf.o presets.o ui.o gpio_monitor.o
DEPS = $(OBJS:.o=.d)

sampler: $(OBJS)
	$(CC) $(CFLAGS) -o sampler $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

-include $(DEPS)

clean:
	rm -f *.o *.d sampler
