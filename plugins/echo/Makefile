CC = gcc
CFLAGS  = -fPIC -Wall -pipe -DDEBUG -g -D_GNU_SOURCE -D_REENTRANT
LDFLAGS = -Wl,-e,__echo_plugin_main
LIB 	= -ldl -lrt -rdynamic
INC     = -I../../inc 
OO	= echo.o
TARGETS = libecho.so
all: $(TARGETS)

$(TARGETS): $(OO)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OO) -shared -o $@ $(LIBDIR) $(LIB)

install:
	install $(TARGETS) ../../bin/
	cp echo.conf ../../bin/

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(TARGETS)
