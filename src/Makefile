DEBUG = -g -DDEBUG
CC = gcc
CFLAGS  = $(DEBUG) -Wall
LIB 	= -ldl -rdynamic -lpthread
INC     = -I../inc
OO	= verben.o dll.o log.o conf.o lock.o shmq.o notifier.o \
      anet.o dlist.o worker.o conn.o ae.o sds.o daemon.o
TARGETS = verben

all: $(TARGETS) $(BIN)

$(TARGETS): $(OO)
	$(CC) $(CFLAGS) $(OO) -o $@ $(LIBDIR) $(LIB)

install:
	install $(TARGETS) ../bin/

#deps
shmq.o: lock.c shmq.c

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(TARGETS)
