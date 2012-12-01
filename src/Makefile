DEBUG = -g -DDEBUG
CC = gcc
CFLAGS  = $(DEBUG) -Wall
LIB 	= -ldl -rdynamic
INC     = -I../inc
VERBENOO = verben.o dll.o log.o conf.o lock.o shmq.o notifier.o \
      anet.o dlist.o worker.o conn.o ae.o sds.o daemon.o hash.o \
	  vector.o
BENCHOO = echo_benchmark.o dlist.o ae.o sds.o anet.o
VERBEN = verben
VERS = version.h
BENCH = echo_benchmark

all: $(VERBEN) $(BENCH)

$(VERS):
	cd ../inc && sh version.h.sh 

$(VERBEN): $(VERS) $(VERBENOO)
	$(CC) $(CFLAGS) $(VERBENOO) -o $@ $(LIBDIR) $(LIB)

$(BENCH): $(BENCHOO)
	$(CC) $(CFLAGS) $(BENCHOO) -o $@ $(LIBDIR) $(LIB)

install:
	install $(VERBEN) ../bin/
	install $(BENCH) ../bin/

#deps
shmq.o: lock.c shmq.c
conf.o: hash.c conf.c

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(VERBEN)
	rm -f $(BENCH)
