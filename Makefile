build:
	gcc -Wall server.c -o server
	gcc -Wall subscriber.c -o subscriber
clean:
	-rm -f *.o subscriber server
