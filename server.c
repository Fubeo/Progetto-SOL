#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "./lib/customsocket.h"
#include "./lib/customqueue.h"
#include "./lib/customconfig.h"
#include "./lib/customerrno.h"
#include "./lib/customlist.h"
#include "./lib/customfile.h"
#include "./lib/customhashtable.h"
#include "./lib/customsortedlist.h"

#define CONFIG "./config/test1.ini"

#define MASTER_WAKEUP_SECONDS 3
#define MASTER_WAKEUP_MS 0

// IMPORTANTE: UNIRE FILE_S E RELATIVE FUNZIONI A CUSTOMFILE.H
typedef struct {
    char *path;
    void *content;
    size_t size;
    list *pidlist;
} file_s;


// Configurazione server
bool server_running =  true;
settings config = DEFAULT_SETTINGS;
int close_type = 0;   // 0 -> nessuna chiusura, 1 -> SIGINT/SIGQUIT, 2 -> SIGHUP

// Strutture necessarie per la gestione del master-workers
queue *queue_clients;
int n_clients = 0;
int pipe_fd[2];

// Strutture necessarie per la gestione dello storage interno
list *storage_fifo;
size_t storable_files_left;
size_t storage_left;
hash_table *tbl_file_path;
hash_table *tbl_has_opened;
size_t n_rimpiazzamenti_cache = 0;

bool soft_close = false;


//==============================================================================
//                                  Funzioni
// =============================================================================

void init_server(char *config_path);
void *worker_function();
void *stop_server(void *argv);


int main() {
  // Dichiarazione socket
  int fd_sk_server = -1, fd_sk_client = -1;

  init_server(CONFIG);

  // creo una lista per inserire i socket
  sorted_list *fd_list = sortedlist_create();

  // socket()
  fd_sk_server = server_unix_socket(config.SOCK_PATH);

  // bind() e listen()
  server_unix_bind(fd_sk_server, config.SOCK_PATH);
  printf("Ready for fd_sk_client connect().\n");

  // creazione e inizializzazione thread pool
  pthread_t tid = 0;
  pthread_t thread_pool[config.N_WORKERS];

  fprintf(stdout, "Thread pool: [ ");

  for (int i = 0; i < config.N_WORKERS; i++) {
      pthread_create(&tid, NULL, &worker_function, NULL);
      thread_pool[i] = tid;
      fprintf(stdout, "%ld ", tid);
  }
  fprintf(stdout, "]\n");


  pthread_attr_t thattr = {0};
  pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&tid, &thattr, &stop_server, NULL) != 0) {
      fprintf(stderr, "Errore: impossibile avviare il Server in modo sicuro\n");
      return -1;
  }

  fd_set current_sockets;

  FD_ZERO(&current_sockets);
  FD_SET(fd_sk_server, &current_sockets);
  FD_SET(pipe_fd[0], &current_sockets);
  sortedlist_insert(&fd_list, fd_sk_server);
  sortedlist_insert(&fd_list, pipe_fd[0]);

  int sreturn;
  psucc("[Server in Ascolto]\n\n");

  while (server_running) {
      fd_set ready_sockets = current_sockets;


      struct timeval tv = {MASTER_WAKEUP_SECONDS, MASTER_WAKEUP_MS};
      if ((sreturn = select(sortedlist_getMax(fd_list) + 1, &ready_sockets, NULL, NULL, &tv)) < 0) {
          if (errno != EINTR) {
              fprintf(stderr, "Select Error: value < 0\n"
                              "Error code: %s\n\n", strerror(errno));
          }
          server_running = false;
          break;
      }
              fprintf(stdout, "%d\n", sreturn);

      if (soft_close && n_clients == 0) {
          break;
      }

      if (sreturn > 0) {
        fprintf(stdout, "In attesa di un client...\n");
        sortedlist_iterate();
        for (int i = 0; i <= sortedlist_getMax(fd_list); i++) {

          int set_fd = sortedlist_getNext(fd_list);

          if (FD_ISSET(set_fd, &ready_sockets)) {

            if (set_fd == fd_sk_server) {
              int fd_sk_client = server_unix_accept(fd_sk_server);

              if (fd_sk_client != -1) {
                if (soft_close) {

                    pwarn("Client %d rifiutato\n", fd_sk_client);

                  sendInteger(fd_sk_client, CONNECTION_REFUSED);
                  close(fd_sk_client);
                  break;
                }
                  printf("Client %d connesso\n", fd_sk_client);

                sendInteger(fd_sk_client, CONNECTION_ACCEPTED);

                char *cpid = receiveStr(fd_sk_client);

                fprintf(stdout, "CLIENT cpid:%d\n", cpid);

                int *n = malloc(sizeof(int));
                if (n == NULL) {
                    fprintf(stderr, "Impossibile allocare per nuovo client\n");
                    return errno;
                }
                *n = 0;
                hash_insert(&tbl_has_opened, cpid, n);
                free(cpid);
                n_clients++;
              }
              FD_SET(fd_sk_client, &current_sockets);
              sortedlist_insert(&fd_list, fd_sk_client);
              break;

          } else if (set_fd == pipe_fd[0]) {
            int old_fd_c;
            readn(pipe_fd[0], &old_fd_c, sizeof(int));
            FD_SET(old_fd_c, &current_sockets);
            sortedlist_insert(&fd_list, old_fd_c);

            break;
          } else {
            FD_CLR(set_fd, &current_sockets);
            sortedlist_remove(&fd_list, set_fd);
            queue_insert(&queue_clients, set_fd);
            break;
          }
        }
      }
    }
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

  // creazione coda di client
  queue_clients = queue_create();
  pipe(pipe_fd);

  // inizializzazione strutture storage
  storage_fifo = list_create();
  tbl_file_path = hash_create(config.MAX_STORABLE_FILES);
  tbl_has_opened = hash_create(config.MAX_STORABLE_FILES);
  storage_left = config.MAX_STORAGE;
  storable_files_left = config.MAX_STORABLE_FILES;

  if (config.MAX_STORAGE >= INT_MAX) {
    fprintf(stderr, "MAX_STORAGE CANNOT BE HIGHER THAN MAX INT REACHED\n");
    exit(-1);
  }

}

void *worker_function(){
  while(1){
    // appena e' disponibile, estrai un fd_sk_client dalla coda
    int fd_sk_client = queue_get(&queue_clients);
    if(fd_sk_client == -1) break;   // se non gli viene associato nessun fd_sk_client

    bool is_client_connected = true;

  		fprintf(stdout, "ATTENDO NUOVE REQUEST DAL CLIENT\n\n");

      char *request = receiveStr(fd_sk_client);

      fflush(stdout);
      printf("\n\n\nRICHIESTA DEL CLIENT %d: %s\n", fd_sk_client, request);

      if (!str_is_empty(request)) {
        switch (request[0]) {

        }
      }

      free(request);

      sleep(3);

    }

}


void *stop_server(void *argv) {
    sigset_t set;
    int signal_captured;
    int t = -1;

    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGHUP);


        psucc("SIGWAIT Thread avviato\n\n");


    pthread_sigmask(SIG_SETMASK, &set, NULL);

    if (sigwait(&set, &signal_captured) != 0) {
        soft_close = true;
        return NULL;
    }

    if (signal_captured == SIGINT || signal_captured == SIGQUIT) {   //SIGINT o SIGQUIT -> uscita forzata
        server_running = false;
    } else if (signal_captured == SIGHUP || signal_captured == SIGTERM) { //SIGHUP o SIGTERM -> uscita soft
        soft_close = true;
    }

    writen(pipe_fd[1], &t, sizeof(int)); //sveglio la select scrivendo nella pipe
    return argv;
}
