PROGS=server

# Compiler and linker flags
CFLAGS+=-Wall -I../include
LDFLAGS+=-L../lib
LDLIBS+=-ldirtree

all: mylib.so $(PROGS)

# Rule for mylib.o
mylib.o: mylib.c
	gcc $(CFLAGS) -fPIC -DPIC -c mylib.c

# Rule for mylib.so
mylib.so: mylib.o
	ld -shared -o mylib.so mylib.o -ldl $(LDFLAGS)

# Clean rule
clean:
	rm -f *.o *.so $(PROGS)