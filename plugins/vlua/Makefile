CC = gcc
CFLAGS  = -fPIC -Wall -pipe -DDEBUG -g -D_GNU_SOURCE -D_REENTRANT
LDFLAGS = -Wl,-e,__vlua_plugin_main
LIB 	= -ldl -lrt -rdynamic -llua
INC     = -I../../inc 
OO	= vlua.o
TARGETS = libvlua.so

all: $(TARGETS)


$(TARGETS): $(OO)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OO) -shared -o $@ $(LIBDIR) $(LIB)

install:
	install $(TARGETS) ../../bin/
	cp vlua.conf ../../bin/

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(TARGETS)
