CC = gcc
CFLAGS = -g -I. -Wall 
LDFLAGS = -pthread

ALL=server_coarse server_fine server_rw interface

all:	$(ALL)

server_coarse: server.o db_coarse.o window.o words.o
	$(CC) $(CFLAGS) $(LDFLAGS) server.o db_coarse.o window.o words.o -o server_coarse

server_fine: server.o db_fine.o window.o words.o 
	$(CC) $(CFLAGS) $(LDFLAGS) server.o db_fine.o window.o words.o -o server_fine

server_rw: server.o db_rw.o window.o words.o 
	$(CC) $(CFLAGS) $(LDFLAGS) server.o db_rw.o window.o words.o -o server_rw
interface: interface.o
	$(CC) $(CFLAGS) $(LDFLAGS) interface.o -o interface

clean:
	/bin/rm -f *.o $(ALL) a.out core *.core
