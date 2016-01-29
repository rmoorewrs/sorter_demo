CC=gcc
CFLAGS=-I. -I/usr/include/modbus -g
LIBS= -lm -lmodbus

DEPS = vplc.h sorter_demo.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: sorter_demo

sorter_demo: sorter_demo.o vplc.o  
	$(CC) -o sorter_demo sorter_demo.o vplc.o $(CFLAGS) $(LIBS)


clean:
	rm sorter_demo.o vplc.o sorter_demo 

