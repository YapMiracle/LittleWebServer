all: http

http: httpd.c
	gcc -W -Wall -o http httpd.c -lpthread

clean:
	rm http
