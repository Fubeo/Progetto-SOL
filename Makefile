SHELL      		= /bin/bash
CC         		= gcc
INCLUDES   		= -I./lib/*.h
LIBS       		= -lpthread
SERVER_OUT 		= ./out/server
CLIENT_OUT 		= ./out/client
FLAGS					= -std=c99 -Wall
.DEFAULT_GOAL := all

server_lib = 	./lib/src/customsocket.c	\
							./lib/src/customlist.c		\
							./lib/src/customstring.c

client_lib = 	./lib/src/customsocket.c


.SUFFIXES: .c .h
.PHONY: all

server: server.c
		$(CC) $(INCLUDES) -o $(SERVER_OUT) server.c $(server_lib) $(LIBS) $(FLAGS)

client: client.c
		$(CC) $(INCLUDES) -o $(CLIENT_OUT) client.c $(client_lib) $(LIBS) $(FLAGS)

all: 		server client

test1: server client
	clear
	valgrind --leak-check=full $(SERVER_OUT) &
	sh script/test1.sh
	@killall -TERM -w memcheck-amd64-
	@printf "\ntest1 terminato\n"
