all: sproxy cproxy

sproxy: sproxy.c
	gcc -std=c99 -Wall -o sproxy sproxy.c

cproxy: cproxy.c
	gcc -std=c99 -Wall -o cproxy cproxy.c

clean: cleansproxy cleancproxy

cleansproxy:
	-rm -f sproxy *.o

cleancproxy:
	-rm -f cproxy *.o