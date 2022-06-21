/**************************************************************************/
/* This example program provides code for a server application that uses     */
/* AF_UNIX address family                                                 */
/**************************************************************************/

/**************************************************************************/
/* Header files needed for this sample program                            */
/**************************************************************************/
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include "./lib/customsocket.h"
#include "./lib/customqueue.h"


// DEVONO ESSERE SOSTITUITI CON CONTENUTO FILE CONFIG
#define N_WORKERS 8
#define SERVER_PATH     "./socket/serversock"

// Strutture necessarie per master-workers
queue *q;

int n_clients = 0;

int pipe_fd[2];
int server_running = 5;
int soft_close = 0;

void *worker_function(){

  // appena e' disponibile, estrai un client dalla coda
  int fd_sk_client = queue_get(&q);

  char *buffer;
  buffer = receiveStr(fd_sk_client);

  fprintf(stdout, "Thread: %d, Client: %s\n", pthread_self(), buffer);

  if(fd_sk_client == 4)sleep(1);

  char msg[100] = "Bel nome ";
  strcat(msg, buffer);
  sendStr(fd_sk_client, msg);

  if (fd_sk_client != -1)
    close(fd_sk_client);

}

int main() {
  // Dichiarazione socket
  int    fd_sk_server = -1, fd_sk_client = -1;

  //char   buffer[BUFFER_LENGTH];
  struct sockaddr_un serveraddr;

  // socket()
  fd_sk_server = server_unix_socket(SERVER_PATH);

  // bind() e listen()
  server_unix_bind(fd_sk_server, SERVER_PATH);
  printf("Ready for client connect().\n");

  // creazione coda di client
  q = queue_create();

  // creazione e inizializzazione thread pool
  pthread_t tid = 0;
  pthread_t thread_pool[N_WORKERS];

  fprintf(stdout, "Thread pool: [ ");

  for (int i = 0; i < N_WORKERS; i++) {
      pthread_create(&tid, NULL, &worker_function, NULL);
      thread_pool[i] = tid;
      fprintf(stdout, "%d ", tid);
  }

  fprintf(stdout, "]\n");

  while(server_running--){
    printf("%d\n", server_running);
    // accept()
    fd_sk_client = server_unix_accept(fd_sk_server);

    if (soft_close) {   // se e' stata richiesta una soft close
        printf("Client %d rifiutato\n", fd_sk_client);

        close(fd_sk_client);
        break;
    }
    // se non e' stata richiesta una soft close

    printf("Client %d connesso\n", fd_sk_client);

    n_clients++;
    queue_insert(&q, fd_sk_client);

    if (soft_close) {
      server_running = 0;
    }
  }



  for (int i = 0; i < N_WORKERS; i++) {
      fprintf(stdout, "Sto aspettando %d\n", thread_pool[i]);
      pthread_join(thread_pool[i], NULL);
      fprintf(stdout, "Ho aspettato %d\n", thread_pool[i]);
  }

  // chiudo il socket del server
  if (fd_sk_server != -1)
    close(fd_sk_server);
}
