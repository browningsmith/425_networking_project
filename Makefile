all: sproxy cproxy

sproxy: sproxy.c
	gcc -std=c99 -o sproxy sproxy.c

cproxy: cproxy.c
	gcc -std=c99 -o cproxy cproxy.c

clean: cleansproxy cleancproxy

cleansproxy:
	-rm -f sproxy *.o

cleancproxy:
	-rm -f cproxy *.o