Makefile

Prima di eseguire il programma:
make folders      # solo se scaricato con git clone
chmod 777 scripts/*

Comandi Makefile
make client -> compila client.c con i rispettivi file di include
make server -> compila server.c con i rispettivi file di include
make all -> client + server
make clean -> rimuove i file contenuti nelle cartelle out (file generati dalla compilazione) e tmp
make cleanlogs -> rimuove i file di log
make cleanall -> clean + cleanlogs
make folders -> crea le cartelle necessarie per l'esecuzione del programma
make stats -> restituisce le statistiche calcolate dallo script statistiche.sh
make test1 , make test2 , make test3 -> clean + compilazione + esecuzione dei test
