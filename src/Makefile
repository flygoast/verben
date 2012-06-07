DEBUG = -g -DDEBUG
CC = gcc
CFLAGS  = $(DEBUG)
LIB 	= -ldl -rdynamic
INC     = -I../inc
OO	= verben.o dll.o log.o config.o
TARGETS = verben

all: $(TARGETS) $(BIN)

$(TARGETS): $(OO)
	$(CC) $(CFLAGS) $(OO) -o $@ $(LIBDIR) $(LIB)

install:
	install $(TARGETS) ../bin/

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(TARGETS)
