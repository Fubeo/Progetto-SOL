SHELL      				= /bin/bash
CC         				= gcc
INCLUDES   				= -I./lib/*.h
LIBS       				= -lpthread
SERVER_OUT 				= ./out/server
CLIENT_OUT 				= ./out/client
FLAGS							= -std=c99 -Wall
VALGRIND_FLAGS 		= --leak-check=full --show-leak-kinds=all -s

.DEFAULT_GOAL := all

server_lib = 	./lib/src/customsocket.c			\
							./lib/src/customlist.c				\
							./lib/src/customsortedlist.c	\
							./lib/src/customqueue.c				\
							./lib/src/customstring.c			\
							./lib/src/customconfig.c			\
							./lib/src/customfile.c				\
							./lib/src/customprint.c				\
							./lib/src/customhashtable.c		\
							./lib/src/customlog.c

client_lib = 	./lib/src/customsocket.c			\
							./lib/src/customprint.c				\
							./lib/src/customstring.c			\
							./lib/src/customfile.c				\
							./lib/src/clientapi.c				\
							./lib/src/customlist.c


.SUFFIXES: .c .h
.PHONY: all

clean:
		@rm -f tmp/download/* tmp/backup/* tmp/*.sk out/*

cleanlogs:
		@rm -f logs/*

cleanall: clean cleanlogs

stats:
		./scripts/statistiche.sh

server: server.c
		$(CC) $(INCLUDES) -o $(SERVER_OUT) server.c $(server_lib) $(LIBS) $(FLAGS)

client: client.c
		$(CC) $(INCLUDES) -o $(CLIENT_OUT) client.c $(client_lib) $(LIBS) $(FLAGS)

all: server client

test1: clean server client
		clear
		valgrind $(VALGRIND_FLAGS) $(SERVER_OUT) -c./config/test1.ini &
		sh scripts/test1.sh
		@killall -TERM -w memcheck-amd64-
		@printf "\ntest1 terminated\n"

testlock: clean server client clean
		clear
		valgrind $(VALGRIND_FLAGS) $(SERVER_OUT) -c./config/testlock.ini &
		sh scripts/testlock.sh
		@killall -TERM -w memcheck-amd64-
		@printf "\ntestlock terminated\n"
