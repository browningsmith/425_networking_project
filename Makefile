all: server client

server: server.c
	gcc -std=c99 -o server server.c

client: client.c
	gcc -std=c99 -o client client.c

clean: cleanserver cleanclient

cleanserver:
	-rm -f server *.o

cleanclient:
	-rm -f client *.o