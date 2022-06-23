#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>
#include "./lib/customsocket.h"
#include "./lib/customqueue.h"
#include "./lib/customconfig.h"

#define CONFIG "./config/test1.ini"

// Configurazione server
settings config = DEFAULT_SETTINGS;
int server_running = 5;
int close_type = 0;   // 0 -> nessuna chiusura, 1 -> SIGINT/SIGQUIT, 2 -> SIGHUP


// Strutture necessarie per master-workers
queue *queue_clients;
int n_clients = 0;


// Funzioni

void init_server(char *config_path);
void *worker_function();

int main() {
  // Dichiarazione socket
  int fd_sk_server = -1, fd_sk_client = -1;

  init_server(CONFIG);

  // socket()
  fd_sk_server = server_unix_socket(config.SOCK_PATH);

  // bind() e listen()
  server_unix_bind(fd_sk_server, config.SOCK_PATH);
  printf("Ready for client connect().\n");


  // creazione e inizializzazione thread pool
  pthread_t tid = 0;
  pthread_t thread_pool[config.N_WORKERS];

  fprintf(stdout, "Thread pool: [ ");

  for (int i = 0; i < config.N_WORKERS; i++) {
      pthread_create(&tid, NULL, &worker_function, NULL);
      thread_pool[i] = tid;
      sleep(1);
      fprintf(stdout, "%ld ", tid);
  }

  fprintf(stdout, "]\n");

  while(server_running--){
    //printf("%d\n", server_running);


    // accept()
    fd_sk_client = server_unix_accept(fd_sk_server);

    //if (soft_close) {   // se e' stata richiesta una soft close
    //    printf("Client %d rifiutato\n", fd_sk_client);

    //    close(fd_sk_client);
    //    break;
    //}
    // se non e' stata richiesta una soft close

    printf("Client %d connesso\n", fd_sk_client);

    n_clients++;
    queue_insert(&queue_clients, fd_sk_client);

    //if (soft_close) {
    //  server_running = 0;
    //}
  }
  sleep(1);
  queue_close(&queue_clients);

  for (int i = config.N_WORKERS-1; i >-1; i--) {
      fprintf(stdout, "Sto aspettando %ld\n", thread_pool[i]);
      pthread_join(thread_pool[i], NULL);
      fprintf(stdout, "Ho aspettato %ld\n", thread_pool[i]);
  }

  // chiudo il socket del server
  if (fd_sk_server != -1)
    close(fd_sk_server);
}

void init_server(char *config_path) {

    // lettura file config
    settings_load(&config, config_path);

    fprintf(stdout, "%d, %s\n", config.N_WORKERS, config.SOCK_PATH);

    // creazione coda di client
    queue_clients = queue_create();

}

void *worker_function(){
  while(1){
    // appena e' disponibile, estrai un client dalla coda
    int fd_sk_client = queue_get(&queue_clients);
    if(fd_sk_client == -1) break;   // se non gli viene associato nessun client

    char *buffer;
    buffer = receiveStr(fd_sk_client);

    fprintf(stdout, "Thread: %ld, Client: %s\n", pthread_self(), buffer);

    //if(fd_sk_client == 4) sleep(1);

    char msg[100] = "Bel nome ";
    strcat(msg, buffer);
    sendStr(fd_sk_client, msg);

    if (fd_sk_client != -1) close(fd_sk_client);
  }

}
