CC = gcc
CFLAGS  = -fPIC -Wall -pipe -DDEBUG -g -D_GNU_SOURCE -D_REENTRANT
LDFLAGS = -Wl,-e,__http_plugin_main
LIB 	= -ldl -lrt -rdynamic
INC     = -I../../inc 
OO	= http.o
TARGETS = libhttp.so

all: $(TARGETS)


$(TARGETS): $(OO)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OO) -shared -o $@ $(LIBDIR) $(LIB)

install:
	install $(TARGETS) ../../bin/
	cp http.conf ../../bin/
	cp index.html ../../bin/

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INC)

clean:
	rm -f *.o
	rm -f $(TARGETS)
