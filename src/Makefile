DEBUG = -g -DDEBUG -DEPOLL_MODE
CC = gcc
CFLAGS  = $(DEBUG) -Wall
LIB 	= -ldl -rdynamic -lpthread
INC     = -I../inc
VERBENOO = verben.o dll.o log.o conf.o lock.o shmq.o notifier.o \
      anet.o dlist.o worker.o conn.o ae.o sds.o daemon.o
BENCHOO = benchmark.o dlist.o ae.o sds.o anet.o
VERBEN = verben
BENCH = benchmark

all: $(VERBEN) $(BENCH)

$(VERBEN): $(VERBENOO)
	$(CC) $(CFLAGS) $(VERBENOO) -o $@ $(LIBDIR) $(LIB)
$(BENCH): $(BENCHOO)
	$(CC) $(CFLAGS) $(BENCHOO) -o $@ $(LIBDIR) $(LIB)

install:
	install $(VERBEN) ../bin/
	install $(BENCH) ../bin/

#deps
shmq.o: lock.c shmq.c

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(VERBEN)
	rm -f $(BENCH)
