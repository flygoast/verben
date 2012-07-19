CC = gcc
CFLAGS  = -fPIC -Wall -pipe -DDEBUG -g -D_GNU_SOURCE -D_REENTRANT
LIB 	= -ldl -lrt -rdynamic -lpthread
INC     = -I../../inc 
OO	= transfer.o dqueue.o threadpool.o
TARGETS = libtransfer.so

all: $(TARGETS)


$(TARGETS): $(OO)
	$(CC) $(CFLAGS) $(OO) -shared -o $@ $(LIBDIR) $(LIB)

install:
	install $(TARGETS) ../../bin/
	cp transfer.conf ../../bin/

threadpool.o: threadpool.c dqueue.c

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(TARGETS)
