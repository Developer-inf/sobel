.PHONY: all

all: prog

clean:
	rm prog

main.o: main.c
	gcc -Wall -c main.c -o main.o

prog: main.o
	gcc -Wall main.o -lm -o prog -fsanitize=address
	rm *.o
