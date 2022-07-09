    #define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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

#define MASTER_WAKEUP_SECONDS 3
#define MASTER_WAKEUP_MS 0

// ======== struct necessaria per gestione dei file da parte del server ========
typedef struct {
    char *path;
    void *content;
    size_t size;
    list *pidlist;

    pthread_cond_t *file_cond;
    pthread_mutex_t *file_lock;
    char* locked_by;
} file_s;

// =============================================================================
//                         Dichiarazione variabili e struct
// =============================================================================

// =========================== Configurazione server ===========================
bool server_running =  true;
settings config = DEFAULT_SETTINGS;
bool soft_close = false;
pthread_mutex_t server_data_lock = PTHREAD_MUTEX_INITIALIZER;

// ========================== Gestione master-workers ==========================
queue *queue_clients;
int n_clients = 0;
int pipe_fd[2];

// ========================= Gestione storage interno ==========================
list *storage_fifo;
size_t storable_files_left;
size_t storage_left;
hash_table *tbl_file_path;
hash_table *tbl_has_opened;
size_t n_cache_replacements = 0;


//==============================================================================
//                             Dichiarazione funzioni
// =============================================================================

// ===================== Gestione funzioni base del server =====================
void init_server(int argc, char *argv[]);
void *worker_function();
void closeConnection(int client, char *cpid);
void *stop_server(void *argv);
void close_server();

// ======================== Gestione richieste client ==========================
void writeFile(int fd_client, char *request, bool filejustcreated);
void createFile(int fd_client, char *request);
void openFile(int client, char *request);
void openO_LOCK(int client, char *request);
void removeFile(int client, char *request);
void closeFile(int fd_client, char *request);
void readFile(int fd_client, char *request);
void readNFile(int client, char *request);
void send_nfiles(char *key, void *value, bool *exit, void *args);
void appendFile(int client, char *request);
void lockFile(int client, char *request);
void unlockFile(int client, char *request);

// ====================== Gestione storage del server ==========================
void free_space(int fd_client, char option, size_t fsize, char *cpid);
void file_destroy(void *f);
void file_destroy_completely(void *f);
void file_open(file_s **f, char *cpid);
bool file_isOpened(file_s *f);
static file_s *file_init(char *path);
void clear_openedFiles(char *key, void *value, bool *exit, void *cpid);
bool file_is_opened_by(file_s *f, char *pid);
bool file_is_empty(file_s *f);
void file_update(file_s **f, void *newContent, size_t newSize);
void client_closes_file(file_s **f, char *cpid);

// =================== Output informazioni sul server ==========================
void print_statistics();
void print_files(char *key, void *value, bool *exit, void *argv);

int main(int argc, char *argv[]) {
  // Dichiarazione socket
  int fd_sk = -1;

  init_server(argc, argv);

  // creo una lista per inserire i socket
  sorted_list *fd_list = sortedlist_create();

  // socket()
  fd_sk = server_unix_socket(config.SOCK_PATH);

  // bind() e listen()
  server_unix_bind(fd_sk, config.SOCK_PATH);
  printf("Ready for fd_client connect().\n");

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

  // Creazione di un thread per la gestione dei segnali
  pthread_attr_t thattr = {0};
  pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&tid, &thattr, &stop_server, NULL) != 0) {
      fprintf(stderr, "Errore: impossibile avviare il Server in modo sicuro\n");
      return -1;
  }

  // Inserimento della pipe e del socket del server nella lista di socket
  fd_set current_sockets;
  FD_ZERO(&current_sockets);
  FD_SET(fd_sk, &current_sockets);
  FD_SET(pipe_fd[0], &current_sockets);
  sortedlist_insert(&fd_list, fd_sk);
  sortedlist_insert(&fd_list, pipe_fd[0]);

  int sreturn;
  pcolor(CYAN,"=== SERVER LISTENING ============================================\n\n");

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

      if (soft_close && n_clients == 0) {
          break;
      }

      if (sreturn > 0) {
        sortedlist_iterate();
        for (int i = 0; i <= sortedlist_getMax(fd_list); i++) {

          int set_fd = sortedlist_getNext(fd_list);


          if (FD_ISSET(set_fd, &ready_sockets)) {

            if (set_fd == fd_sk) {
              int fd_client = server_unix_accept(fd_sk);

              if (fd_client != -1) {
                if (soft_close) {
                  if(config.PRINT_LOG > 0)
                    pwarn("Client %d refused\n", fd_client);

                  sendInteger(fd_client, CONNECTION_REFUSED);
                  close(fd_client);
                  break;
                }
                if(config.PRINT_LOG > 0)
                  printf("Client %d connected\n", fd_client);

                sendInteger(fd_client, CONNECTION_ACCEPTED);

                char *cpid = receiveStr(fd_client);



                int *n = malloc(sizeof(int));
                if (n == NULL) {
                    fprintf(stderr, "Unable to allocate memory for a new client\n");
                    return errno;
                }
                *n = 0;
                hash_insert(&tbl_has_opened, cpid, n);
                free(cpid);
                n_clients++;
              }
              FD_SET(fd_client, &current_sockets);
              sortedlist_insert(&fd_list, fd_client);
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

  queue_close(&queue_clients);

  for (int i = config.N_WORKERS-1; i >-1; i--) {
      pthread_join(thread_pool[i], NULL);
  }

  // chiudo il socket del server
  if (fd_sk != -1)
    close(fd_sk);

  print_statistics();

  // Libero spazio in memoria
  sortedlist_destroy(&fd_list);
  close_server();
}

//==============================================================================
//                             Definizione funzioni
// =============================================================================


void init_server(int argc, char *argv[]) {

  char *config_path = NULL;
  if (argc == 2) {
      if (str_starts_with(argv[1], "-c")) {
          char *cmd = (argv[1]) += 2;
          config_path = realpath(cmd, NULL);
          if (config_path == NULL) {
              fprintf(stderr, "File config non trovato!\n"
                              "Verranno usate le impostazioni di Default\n\n");
          }
      }
  } else if (argc > 2) {
      pwarn("ATTENZIONE: comando -c inserito non valido\n\n");
  }
  // lettura file config
  settings_load(&config, config_path);
  free(config_path);

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

  // Gestione dei segnali
  sigset_t mask;
  sigfillset(&mask);
  pthread_sigmask(SIG_SETMASK, &mask, NULL);

}

void *worker_function(){
  while(1){
    // appena e' disponibile, estrai un client dalla coda
    int fd_client = queue_get(&queue_clients);
    if(fd_client == -1) break;   // se non gli viene associato nessun client


    char *request = receiveStr(fd_client);

    fflush(stdout);
    pcolor(BLUE, "\nReceived a new request by client %d: %s\n", fd_client, request);

    if (!str_is_empty(request)) {
      switch (request[0]) {
        case 'a': {
          char *cmd = str_cut(request, 2, str_length(request) - 2);
          appendFile(fd_client, cmd);
          free(cmd);
          break;
        }

        case 'c': {
          char *cmd;
          if (request[1] == 'l') {
            cmd = str_cut(request, 3, str_length(request) - 3);
            closeFile(fd_client, cmd);
            free(cmd);
          } else if (request[1] == 'o') {  // Richiesta apertura O_LOCK
            cmd = str_cut(request, 3, str_length(request) - 3);
            openO_LOCK(fd_client, cmd);
            free(cmd);
          } else {
            cmd = str_cut(request, 2, str_length(request) - 2);
            createFile(fd_client, cmd);

            free(cmd);
          }
          break;
        }

        case 'o': {
            char *cmd = str_cut(request, 2, str_length(request) - 2);
            openFile(fd_client, cmd);
            free(cmd);
            break;
        }

        case 'w': {
          char *cmd = str_cut(request, 2, str_length(request) - 2);
          fprintf(stdout, "%s\n", cmd);
          writeFile(fd_client, cmd, false);
          free(cmd);
          break;
        }

        case 'r': {
          char *cmd;
          if (request[1] == 'n') {
            cmd = str_cut(request, 3, str_length(request) - 3);
            readNFile(fd_client, cmd);
          } else if (request[1] == 'm') {
            cmd = str_cut(request, 3, str_length(request) - 3);
            removeFile(fd_client, cmd);
          } else {
            cmd = str_cut(request, 2, str_length(request) - 2);
            readFile(fd_client, cmd);
          }
          free(cmd);

          break;
        }

        case 'l':{
          char *cmd = str_cut(request, 2, str_length(request) - 2);
          lockFile(fd_client, cmd);
          free(cmd);
          break;
        }

        case 'u':{
          char *cmd = str_cut(request, 2, str_length(request) - 2);
          unlockFile(fd_client, cmd);
          free(cmd);
          break;
        }

        case 'e': {
          char *cmd = str_cut(request, 2, str_length(request) - 2);
          closeConnection(fd_client, cmd);
          free(cmd);
          break;
        }
      }
    }
    if (request[0] != 'e' || str_is_empty(request)) {
      if (writen(pipe_fd[1], &fd_client, sizeof(int)) == -1) {
        fprintf(stderr, "An error occurred on write back client to the pipe\n");
        exit(errno);
      }
    }
    free(request);
  }
  return NULL;
}

void closeConnection(int client, char *cpid) {
    int nfiles = *((int *) hash_getValue(tbl_has_opened, cpid));

    if (nfiles == 0) {
        sendInteger(client, S_SUCCESS);
    } else if(config.PRINT_LOG == 2){
        pwarn("Client %d has not closed many files yet\n", client);

        sendInteger(client, SFILES_FOUND_ON_EXIT);

        hash_iterate(tbl_file_path, &clear_openedFiles, (void *) cpid);

    }


    assert((*((int *) hash_getValue(tbl_has_opened, cpid))) == 0);

    hash_deleteKey(&tbl_has_opened, cpid, &free);
    if (close(client) != 0) {
        perr("WARNING: error closing socket with client %d\n", client);
    } else psucc("Client %d disconnected\n\n", client);

    n_clients--;
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


    pthread_sigmask(SIG_SETMASK, &set, NULL);

    psucc("SIGWAIT Thread avviato\n\n");
    if (sigwait(&set, &signal_captured) != 0) {
        soft_close = true;
        return NULL;
    }

    if (signal_captured == SIGINT || signal_captured == SIGQUIT) {        // SIGINT o SIGQUIT -> uscita forzata
        server_running = false;
    } else if (signal_captured == SIGHUP || signal_captured == SIGTERM) { // SIGHUP o SIGTERM -> uscita soft
        soft_close = true;
    }

    writen(pipe_fd[1], &t, sizeof(int)); //sveglio la select scrivendo nella pipe
    return argv;
}

void close_server() {
    settings_free(&config);
    hash_destroy(&tbl_file_path, &file_destroy_completely);
    hash_destroy(&tbl_has_opened, &free);
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    list_destroy(&storage_fifo, NULL);
    queue_destroy(&queue_clients);
}

void createFile(int fd_client, char *request) {
  size_t fsize = receiveInteger(fd_client);

  char **split = NULL;
  int n = str_split(&split, request, ":");
  char *filepath = split[0];
  char *cpid = split[1];
  assert(!str_is_empty(filepath) && filepath != NULL);

  if (hash_containsKey(tbl_file_path, filepath)) {
    pwarn("Il client %d ha tentato di creare il file %s, che già esiste sul Server\n", fd_client, (strrchr(filepath,'/')+1));
    perr("Richiesta non eseguita\n");
    sendInteger(fd_client, SFILE_ALREADY_EXIST);

  } else if (fsize > config.MAX_STORAGE) { //se il file è troppo grande
    pwarn("Il client %d ha tentato di mettere un file troppo grande.\n", fd_client);
    perr("Richiesta non eseguita.\n");
    sendInteger(fd_client, SFILE_TOO_LARGE);

  } else if (storable_files_left == 0) {
    pwarn("Rilevata CAPACITY MISS\n", fd_client);

    sendInteger(fd_client, S_STORAGE_FULL);
    free_space(fd_client, 'c', 0, cpid);
    pwarn("Impossibile espellere file\n", fd_client);
    perr("Richiesta non eseguita\n");

  } else {
    assert(hash_containsKey(tbl_has_opened, cpid));

    file_s *f = file_init(filepath); //creo un nuovo file

    if (f == NULL) {
      sendInteger(fd_client, MALLOC_ERROR);
      return;
    }

    // Acquisizione della lock
    pthread_mutex_lock(f->file_lock);

    hash_insert(&tbl_file_path, filepath, f);	// lo memorizzo

    file_open(&f, cpid); 											// lo apro

    storable_files_left--;										// aggiorno il numero di file memorizzabili
    list_insert(&storage_fifo, filepath, f);  // e infine lo aggiungo alla coda fifo

    sendInteger(fd_client, S_SUCCESS);			// notifico il client dell'esito positivo dell'operazione


    // scrittura del file
    char* r = receiveStr(fd_client);
    writeFile(fd_client, r, true);
    free(r);

  }
  str_clearArray(&split, n);
}

void openFile(int client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":");
    char *filepath = array[0];  //path del file inviato dal Client
    char *cpid = array[1];      //pid del client

    if (!hash_containsKey(tbl_file_path, filepath)) { //controllo se il file non è presente nello storage

            pwarn("Il client %d ha eseguito un operazione su un file che non esiste\n", client);



            perr("Richiesta non eseguita");

        sendInteger(client, SFILE_NOT_FOUND);
        str_clearArray(&array, n);
        return;
    }

    file_s *f = hash_getValue(tbl_file_path, filepath);
    if (file_is_opened_by(f, cpid)) { // Controllo se il file è già stato aperto dal Client
        if(config.PRINT_LOG == 2)
            pwarn("Il client %d ha tentato di aprire un file che ha già aperto\n", client);
        if(config.PRINT_LOG == 1)
            perr("request not executed\n");

        sendInteger(client, SFILE_ALREADY_OPENED);
        str_clearArray(&array, n);
        return;
    }

    pthread_mutex_t *fl = f->file_lock;
    pthread_mutex_lock(fl);
    if (!hash_containsKey(tbl_file_path, filepath)){  // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client %d obtained lock, but file was removed\n", client);
      if(config.PRINT_LOG == 1)
          perr("request not executed\n");
      sendInteger(client, SFILE_WAS_REMOVED);
      str_clearArray(&array, n);
      return;
    }
    file_open(&f, cpid);
    pthread_mutex_unlock(fl);

    sendInteger(client, S_SUCCESS);


        psucc("Client %d opened file %s\n", client, (strrchr(filepath,'/')+1));

    str_clearArray(&array, n);
}

void openO_LOCK(int fd_client, char *request){
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];  // path del file inviato dal Client
  char *cpid = malloc(strlen(array[1])*sizeof(char)+1);
  strcpy(cpid, array[1]);      // pid del client

  if (hash_containsKey(tbl_file_path, filepath)) { // Se il file è presente nello storage
    sendInteger(fd_client, SFILE_ALREADY_EXIST);

    file_s *f = hash_getValue(tbl_file_path, filepath);
    if (file_is_opened_by(f, cpid)) { // controllo se il file è già stato aperto dal Client
      if (config.PRINT_LOG == 2) {
        pwarn("Client %d tried to open a file that he has already opened\n", fd_client);
      }

      if (config.PRINT_LOG == 1){
        perr("request not executed\n");
      }
      sendInteger(fd_client, SFILE_ALREADY_OPENED);
      str_clearArray(&array, n);
      free(cpid);
      return;
    }

    file_open(&f, cpid);

    if(f->locked_by != NULL){
      if(config.PRINT_LOG == 2) fprintf(stdout, "Client %s waiting to open file %s in locked mode\n", cpid, f->path);
    }

    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    bool still_exists;
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_lock;

    // Acquisizione della lock
    while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
      if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "FATAL ERROR unlock\n");
    }
    if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client %d obtained lock, but file was removed\n", fd_client);
      if(config.PRINT_LOG == 1)
          perr("request not executed\n");
      sendInteger(fd_client, SFILE_NOT_FOUND);
      str_clearArray(&array, n);
      free(cpid);
      return;
    }

    f->locked_by = cpid;

    sendInteger(fd_client, S_SUCCESS);

    if (config.PRINT_LOG > 0) {
      psucc("Client %d opened file %s in locked mode\n", fd_client, (strrchr(filepath,'/') + 1));
    }

    str_clearArray(&array, n);

  } else {                                        // Se, invece, il file non e' presente nel server
    sendInteger(fd_client, SFILE_NOT_FOUND);

    int response = receiveInteger(fd_client);
    if(response == SFILE_NOT_FOUND){
        if (config.PRINT_LOG == 2){
            pwarn("Client %d tried to create a file that does not exist\n", fd_client);
        }
        if (config.PRINT_LOG == 1){
            perr("request not executed\n\n");
        }
        str_clearArray(&array, n);
        return;
    }
    size_t fsize = receiveInteger(fd_client);

    if (fsize > config.MAX_STORAGE) {             // Se il file è troppo grande
      pwarn("Il client %d ha tentato di mettere un file troppo grande.\n", fd_client);
      perr("Richiesta non eseguita.\n");
      sendInteger(fd_client, SFILE_TOO_LARGE);

    } else if (storable_files_left == 0) {
      pwarn("System detected a CAPACITY MISS\n", fd_client);

      sendInteger(fd_client, S_STORAGE_FULL);
      free_space(fd_client, 'c', 0, cpid);
      pwarn("Impossibile espellere file\n", fd_client);
      perr("Richiesta non eseguita\n");
    } else {
      assert(hash_containsKey(tbl_has_opened, cpid));

      file_s *f = file_init(filepath);//creo un nuovo file

      if (f == NULL) {
        sendInteger(fd_client, MALLOC_ERROR);
        str_clearArray(&array, n);
        return;
      }

      hash_insert(&tbl_file_path, filepath, f);	// lo memorizzo
      file_open(&f, cpid); 											// lo apro

      pthread_mutex_lock(&server_data_lock);
      storable_files_left--;										// aggiorno il numero di file memorizzabili
      pthread_mutex_unlock(&server_data_lock);

      list_insert(&storage_fifo, filepath, f);  // lo aggiungo alla coda fifo

      if(f->locked_by != NULL){
        if(config.PRINT_LOG == 2) fprintf(stdout, "Client %s waiting to open file %s in locked mode\n", cpid, f->path);
      }

      pthread_mutex_lock(f->file_lock);
      f->locked_by = cpid;

      sendInteger(fd_client, S_SUCCESS);			// notifico il client dell'esito positivo dell'operazione
    }

    str_clearArray(&array, n);
  }

}

void removeFile(int client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":");
    char *filepath = array[0];  // path del file inviato dal Client
    char *cpid = array[1];      // pid del client

    // Controllo se il file esiste
    if (!hash_containsKey(tbl_file_path, filepath)) {
            pwarn("Client %d tried to access a non-existent file\n", client);


        sendInteger(client, SFILE_NOT_FOUND);
        str_clearArray(&array, n);
        return;
    }


    file_s *f = hash_getValue(tbl_file_path, filepath);


    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_lock;

    // Acquisizione della lock
    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) pthread_mutex_lock(mtx);

    if(!hash_containsKey(tbl_file_path, filepath)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client %d obtained lock, but file was removed\n", client);
      if(config.PRINT_LOG == 1)
          perr("request not executed\n");
      str_clearArray(&array, n);
      return;
    }

    // Aggiorno i dati del server
    pthread_mutex_lock(&server_data_lock);
    storable_files_left++;
    storage_left += f->size;
    if (storage_left > config.MAX_STORAGE) {
      storage_left = config.MAX_STORAGE;
    }
    pthread_mutex_unlock(&server_data_lock);

    list_node *corr = f->pidlist->head;
    while(corr != NULL){
      fprintf(stdout, "%s\n", corr->key);
      int *x = (int *) hash_getValue(tbl_has_opened, corr->key);
      *x -= 1;
      hash_updateValue(&tbl_has_opened, corr->key, x, NULL);
      corr = corr->next;
    }

    // Rimozione del file dallo storage
    hash_deleteKey(&tbl_file_path, filepath, &file_destroy);

    pthread_cond_signal(cond);
    pthread_mutex_unlock(mtx);

    // Libero la memoria dai mutex dei file
    pthread_cond_destroy(cond);
    pthread_mutex_destroy(mtx);
    free(cond);
    free(mtx);

    sendInteger(client, S_SUCCESS);

    if(config.PRINT_LOG > 0)
        psucc("File %s removed by client %d\n\n", (strrchr(filepath,'/')+1), client);

    str_clearArray(&array, n);
}

void closeFile(int fd_client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":");
    assert(n == 2);


    char *filepath = array[0];
    char *cpid = array[1];


    if (!hash_containsKey(tbl_file_path, filepath)) {

        pwarn("Il client %d ha eseguito un operazione su un file che non esiste\n", fd_client);

        sendInteger(fd_client, SFILE_NOT_FOUND);
        str_clearArray(&array, n);
        return;
    }


    file_s *f = hash_getValue(tbl_file_path, filepath);

    if (!file_is_opened_by(f, cpid)) {
      pwarn("Il client %d ha eseguito un operazione su un file non aperto\n", fd_client);


        sendInteger(fd_client, SFILE_NOT_OPENED);
        str_clearArray(&array, n);
        return;
    }

    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_lock;

    // Acquisizione della lock
    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) pthread_mutex_lock(mtx);

    if(!hash_containsKey(tbl_file_path, filepath)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client %d obtained lock, but file was removed\n", fd_client);
      if(config.PRINT_LOG == 1)
          perr("request not executed\n");
      sendInteger(fd_client, SFILE_WAS_REMOVED);
      str_clearArray(&array, n);
      return;
    }

    client_closes_file(&f, cpid);

    if(f->locked_by != NULL){
      free(f->locked_by);
      f->locked_by = NULL;
    }

    pthread_cond_signal(cond);
    pthread_mutex_unlock(mtx);

    sendInteger(fd_client, S_SUCCESS);

    if(config.PRINT_LOG > 0)
      psucc("File %s closed by client %s\n\n", (strrchr(filepath,'/')+1), cpid);

    str_clearArray(&array, n);
}

void writeFile(int fd_client, char *request, bool filejustcreated) {
    char **split = NULL;
    int n = str_split(&split, request, ":?");
    assert(n == 3);
    char *filepath = split[0];
    char *cpid = split[1];

    char option = (split[2])[0];
    assert(option == 'y' || option == 'n');
    size_t fsize;

    void *fcontent = NULL;

    // Il server riceve il contenuto e la dimensione del file
    receivefile(fd_client, &fcontent, &fsize);


    if (fsize > config.MAX_STORAGE) {
        pwarn("Il client %d ha inviato un file troppo grande\n", fd_client);
        sendInteger(fd_client, SFILE_TOO_LARGE);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    }

    if (!hash_containsKey(tbl_file_path, filepath)) {

    		fprintf(stdout, "FILE CHE NON ESISTE:\n%s/\n", filepath);

        pwarn("Il client %d ha eseguito un'operazione su un file che non esiste\n", fd_client);


        sendInteger(fd_client, SFILE_NOT_FOUND);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    }

    file_s *f = hash_getValue(tbl_file_path, filepath);
    if (!file_is_opened_by(f, cpid)) {
        pwarn("Il client %d ha eseguito un operazione su un file non aperto\n", fd_client);


        sendInteger(fd_client, SFILE_NOT_OPENED);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    } else if (!file_is_empty(f)) {
        pwarn("Il client %d ha eseguito una Write su un file non vuoto\n", fd_client);


        sendInteger(fd_client, SFILE_NOT_EMPTY);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    }

    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_lock;

    // Acquisizione della lock
    if(!filejustcreated && (f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0)) pthread_mutex_lock(mtx);

    if(!hash_containsKey(tbl_file_path, filepath)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client with pid %s obtained lock, but file was removed\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("Request not executed\n");
      sendInteger(fd_client, SFILE_WAS_REMOVED);
      free(fcontent);
      str_clearArray(&split, n);
      return;
    }

    //da qui in poi il file viene inserito
    if (fsize > storage_left) {  //se non ho spazio
        pwarn("Rilevata Capacity Miss\n");

        sendInteger(fd_client, S_STORAGE_FULL);
        free_space(fd_client, option, fsize, cpid);

        if (fsize > storage_left) {
            perr("Non è stato possibile liberare spazio\n");

            free(fcontent);
            str_clearArray(&split, n);
            sendInteger(fd_client, S_FREE_ERROR);
            return;
        }

        printf("Spazio liberato!\n");
    }

    assert(f->path != NULL && !str_is_empty(f->path));
    assert(storage_left <= config.MAX_STORAGE);

    // Scrivo il contenuto nel file
    file_update(&f, fcontent, fsize);

    sendInteger(fd_client, S_SUCCESS);

    // Rilascio la lock
    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) {
      pthread_cond_signal(cond);
      pthread_mutex_unlock(mtx);
    }

    str_clearArray(&split, n);

    psucc("Write completed. Current storage left: %d\n\n", storage_left);
}

void readFile(int fd_client, char *request) {
    assert(!str_is_empty(request) && request != NULL);
    char **array = NULL;
    int n = str_split(&array, request, ":");
    char *filepath = array[0];
    char *cpid = array[1];

    //controllo che il file sia stato creato
    if (!hash_containsKey(tbl_file_path, filepath)) {
        sendInteger(fd_client, SFILE_NOT_FOUND);
        str_clearArray(&array, n);
        return;
    }
    file_s *f = hash_getValue(tbl_file_path, filepath);

    if(!file_is_opened_by(f, cpid)){
      if(config.PRINT_LOG == 2)
          pwarn("Client %d tried to read a not opened file\n", fd_client);
      if(config.PRINT_LOG == 1)
          perr("request not executed\n");

        sendInteger(fd_client, SFILE_NOT_OPENED);
        str_clearArray(&array, n);
        return;
    }

    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_lock;

    // Acquisizione della lock
    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) pthread_mutex_lock(mtx);

    if(!hash_containsKey(tbl_file_path, filepath)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client with pid %s obtained lock, but file was removed\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("request not executed\n");
      sendInteger(fd_client, SFILE_WAS_REMOVED);
      str_clearArray(&array, n);
      return;
    }

    sendInteger(fd_client, S_SUCCESS);

    sendn(fd_client, f->content, f->size);

    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) {
      pthread_cond_signal(cond);
      pthread_mutex_unlock(mtx);
    }

    str_clearArray(&array, n);
}

void readNFile(int fd_client, char *request) {
    assert(!str_is_empty(request) && request != NULL);
    char **array = NULL;
    int n = str_split(&array, request, ":");
    char *nf = array[0];
    char *cpid = array[1];

    if (hash_isEmpty(tbl_file_path)) {
        sendInteger(fd_client, S_STORAGE_EMPTY);
        str_clearArray(&array, n);
        return;
    }

    int n_files_to_send;
    int ret = str_toInteger(&n_files_to_send, nf);
    assert(ret != -1);

    list_node *params = malloc(sizeof(list_node));
    params->key = (void *) &fd_client;
    params->value = (void *) cpid;

    sendInteger(fd_client, S_SUCCESS);
    hash_iteraten(tbl_file_path, &send_nfiles, (void *) params, n_files_to_send);
    sendInteger(fd_client, EOS_F);

    str_clearArray(&array, n);
    free(params);
}

void send_nfiles(char *key, void *value, bool *exit, void *args) {

  list_node *params = (list_node*) args;

  int fd_client = *((int*) params->key);
  char *cpid = (char*) params->value;

  file_s *f = (file_s *) value;

  if(f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    if(config.PRINT_LOG == 2)
        pwarn("Client with pid %s tried to read a locked file\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("request not executed\n");
    return;
  }

  sendInteger(fd_client, !EOS_F);
  sendStr(fd_client, key);
  sendn(fd_client, f->content, f->size);

  //per togliere il warning "parameter never used"
  exit=exit;
  args=args;
}

void appendFile(int client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":?");
    char *filepath = array[0];
    char *cpid = array[1];
    char option = (array[2])[0];

    assert(option == 'y' || option == 'n');

    void *fcontent;
    size_t fsize;
    receivefile(client, &fcontent, &fsize);
    file_s *f = hash_getValue(tbl_file_path, filepath);

    if ((f->size + fsize) > config.MAX_STORAGE) {
        if (config.PRINT_LOG == 2) {
            pwarn("Il client %d ha inviato un file troppo grande\n\n", client);
        }

        sendInteger(client, SFILE_TOO_LARGE);
        free(fcontent);
        return;
    }

    if (!hash_containsKey(tbl_file_path, filepath)) {
        if (config.PRINT_LOG == 2) {
            pwarn("Il client %d ha eseguito un operazione su un file che non esiste\n\n", client);
        }

        sendInteger(client, SFILE_NOT_FOUND);
        free(fcontent);
        str_clearArray(&array, n);
        return;
    }

    if (!file_is_opened_by(f, cpid)) {
        if (config.PRINT_LOG == 2) {
            pwarn("Il client %d ha eseguito un operazione su un file non aperto\n\n", client);
        }
        sendInteger(client, SFILE_NOT_OPENED);
        free(fcontent);
        str_clearArray(&array, n);
        return;
    }

    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_lock;

    // Acquisizione della lock
    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) pthread_mutex_lock(mtx);

    if(!hash_containsKey(tbl_file_path, filepath)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client with pid %s obtained lock, but file was removed\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("Request not executed\n");
      free(fcontent);
      str_clearArray(&array, n);
      sendInteger(client, SFILE_WAS_REMOVED);
      return;
    }

    //da qui in poi il file viene inserito
    if (fsize > storage_left) {  //se non ho spazio
        if (config.PRINT_LOG == 2) {
            pwarn("Detected capacity miss\n");
        }
        sendInteger(client, S_STORAGE_FULL);
        free_space(client, option, fsize, cpid);

        if (fsize >= storage_left) {
            if (config.PRINT_LOG == 1 || config.PRINT_LOG == 2) {
                perr("Non è stato possibile liberare spazio\n\n");
            }
            free(fcontent);
            str_clearArray(&array, n);
            sendInteger(client, S_FREE_ERROR);
            return;
        }

        if (config.PRINT_LOG == 2) {
            printf("Spazio liberato!\n\n");
        }

    }

    size_t newSize = f->size + fsize;
    void *newContent = malloc(newSize);
    if (newContent == NULL) {
        perr("malloc error: impossibile appendere il contenuto richiesto\n");
        sendInteger(client, MALLOC_ERROR);
        return;
    }

    memcpy(newContent, f->content, f->size);
    memcpy(newContent + f->size, fcontent, fsize);

    free(f->content);
    f->content = newContent;
    f->size = newSize;
    sendInteger(client, S_SUCCESS);

    // Rilascio la lock
    if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) {
      pthread_cond_signal(cond);
      pthread_mutex_unlock(mtx);
    }

    if (config.PRINT_LOG == 1 || config.PRINT_LOG == 2) {
        psucc("Append per il Client %d eseguita\n\n", client);
    }
}

void lockFile(int fd_client, char *request){
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];  // path del file inviato dal Client
  char *cpid = malloc(strlen(array[1])*sizeof(char)+1);
  strcpy(cpid, array[1]);      // pid del client

  if (!hash_containsKey(tbl_file_path, filepath)) { // Se il file non è presente nello storage
    if (config.PRINT_LOG == 2) {
      pwarn("Client %d tried to lock a file that does not exist\n", fd_client);
    }
    if (config.PRINT_LOG == 1){
      perr("request not executed\n");
    }
    sendInteger(fd_client, SFILE_NOT_FOUND);
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  file_s *f = hash_getValue(tbl_file_path, filepath);

  if(f->locked_by != NULL)
    if(config.PRINT_LOG == 2) fprintf(stdout, "Client %s waiting to lock file %s\n", cpid, f->path);

  // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
  bool still_exists;
  pthread_cond_t *cond = f->file_cond;
  pthread_mutex_t *mtx = f->file_lock;

  // Acquisizione della lock
  while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "FATAL ERROR unlock\n");
  }
  if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
    if(config.PRINT_LOG == 2)
        pwarn("Client %d obtained lock, but file was removed\n", fd_client);
    if(config.PRINT_LOG == 1)
        perr("request not executed\n");
    sendInteger(fd_client, SFILE_WAS_REMOVED);
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  bool has_opened = file_is_opened_by(f, cpid);
  if (!(has_opened)) { // Controllo se il file è già stato aperto dal Client
    file_open(&f, cpid);
  }

  f->locked_by = cpid;

  sendInteger(fd_client, S_SUCCESS);

  if (config.PRINT_LOG == 1 || config.PRINT_LOG == 2) {
    psucc("Client %d locked file %s\n", fd_client, (strrchr(filepath,'/')+1));
  }

  str_clearArray(&array, n);
}

void unlockFile(int client, char *request){
  assert(!str_is_empty(request) && request != NULL);
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];
  char *cpid = array[1];

  //controllo che il file sia stato creato
  if (!hash_containsKey(tbl_file_path, filepath)) {
    if(config.PRINT_LOG == 2)
        pwarn("Client %d tried to unlock a non-existent file\n", client);
    if(config.PRINT_LOG == 1)
        perr("request not executed\n");

    sendInteger(client, SFILE_NOT_FOUND);
    str_clearArray(&array, n);
    return;
  }

  file_s *f = hash_getValue(tbl_file_path, filepath);

  if(f->locked_by == NULL){
    if(config.PRINT_LOG == 2)
        pwarn("Client %d tried to unlock a non-locked file\n", client);
    if(config.PRINT_LOG == 1)
        perr("request not executed\n");

    sendInteger(client, SFILE_NOT_LOCKED);
    str_clearArray(&array, n);
    return;
  }

  if(strcmp(f->locked_by, cpid) != 0){
    if(config.PRINT_LOG == 2)
        pwarn("Client %d tried to unlock a file locked by another client\n", client);
    if(config.PRINT_LOG == 1)
        perr("request not executed\n");

    sendInteger(client, CLIENT_NOT_ALLOWED);
    str_clearArray(&array, n);
    return;
  }

  free(f->locked_by);
  f->locked_by = NULL;

  pthread_cond_signal(f->file_cond);
  pthread_mutex_unlock(f->file_lock);

  sendInteger(client, S_SUCCESS);

  if(config.PRINT_LOG != 0)
      psucc("Client %d unlocked file %s\n", client, filepath);
  str_clearArray(&array, n);
}


// ====================== Gestione storage del server ==========================

// Funzione di rimozione dei file in caso di Capacity Misses.
void free_space(int fd_client, char option, size_t fsize, char *cpid) {
    list_node *curr = storage_fifo->head;

    while (true) {
        if (curr == NULL) {   //ho finito di leggere la coda
                psucc("Lettura coda FIFO terminata\n\n");
            sendInteger(fd_client, EOS_F);
            return;
        }

        file_s *f = (file_s *) curr->value; //file "vittima"
        assert(f != NULL && hash_containsKey(tbl_file_path, f->path));


        /* Se il file non è aperto, si generano 2 casi prima della rimozione:
         * 1. Il file deve essere inviato al client
         * 2. Il client tenta di creare un file, ma la capacità massima è stata raggiunta. Quindi si
         *    procede come descritto nella Relazione - Sezione "Scelte effettuate"
         */
        if (!file_isOpened(f)) {
            char *filepath = malloc(strlen(f->path)+1*sizeof(char));
            strcpy(filepath, f->path);

            // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
            pthread_cond_t *cond = f->file_cond;
            pthread_mutex_t *mtx = f->file_lock;

            // Acquisizione della lock
            if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) pthread_mutex_lock(mtx);

            if(!hash_containsKey(tbl_file_path, filepath)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
              if(config.PRINT_LOG == 2)
                  pwarn("Client %d obtained lock, but file was removed\n", fd_client);
              if(config.PRINT_LOG == 1)
                  perr("request not executed\n");
              free(filepath);
              break;
            }
            free(filepath);

                printf("Rimuovo il file %s dalla coda\n", (strrchr(f->path, '/')+1));

            if (option == 'y') {  //caso in cui il file viene espulso e inviato al client
                sendInteger(fd_client, !EOS_F);
                sendStr(fd_client, f->path);
                sendn(fd_client, f->content, f->size);
            }

            if (option == 'c') {  //caso in cui il client tenta di creare un file,
                //ma il numero massimo di file memorizzabili è 0. Vengono quindi
                //inviati al Client i nomi dei file che stanno per essere espulsi

                sendInteger(fd_client, !EOS_F);
                sendStr(fd_client, f->path);
            }

            pthread_mutex_lock(&server_data_lock);
            storage_left += f->size;
            if (storage_left > config.MAX_STORAGE) {
                // mi assicuro di rimanere nel range 0 <= storage_left <= MAX_STORAGE
                storage_left = config.MAX_STORAGE;
            }
            storable_files_left++;
            n_cache_replacements++;
            pthread_mutex_unlock(&server_data_lock);

            // Rimozione del file dallo storage
            hash_deleteKey(&tbl_file_path, f->path, &file_destroy);

            pthread_cond_signal(cond);
            pthread_mutex_unlock(mtx);

            // Libero la memoria dai mutex dei file
            pthread_cond_destroy(cond);
            pthread_mutex_destroy(mtx);
            free(cond);
            free(mtx);

            char* key=curr->key;
            curr=curr->next;
            list_remove(&storage_fifo,key,NULL);
        }
        else {
            curr=curr->next;
        }

        if (fsize <= storage_left && storable_files_left > 0) { //raggiunto lo spazio richiesto, esco
            sendInteger(fd_client, EOS_F);
            return;
        }
    }
}

/* Funzione di cancellazione di un file. Viene passata alla hash table quando deve cancellare
 * una chiave, in quanto, la tabella, non può sapere il tipo di dato che sta memorizzando.
*/
void file_destroy(void *f) {
    file_s *file = (file_s *) f;
    free(file->content);
    free(file->path);
    list_destroy(&file->pidlist, NULL);
    free(file->locked_by);
    free(file);
}

void file_destroy_completely(void *f){
  file_s *file = (file_s *) f;
  pthread_mutex_destroy(file->file_lock);
  free(file->file_lock);
  pthread_cond_destroy(file->file_cond);
  free(file->file_cond);
  file_destroy(f);
}

/* Funzione che simula l apertura del file f da parte del client cpid.
 * */
void file_open(file_s **f, char *cpid) {
    list_insert(&(*f)->pidlist, cpid, NULL);

    int *n = (int *) hash_getValue(tbl_has_opened, cpid);
    *n = *n + 1;

    hash_updateValue(&tbl_has_opened, cpid, n, NULL);
}

bool file_isOpened(file_s *f) {
    return !list_isEmpty(f->pidlist);
}

static file_s *file_init(char *path) {
    if (str_is_empty(path))
        return NULL;

    file_s *file = malloc(sizeof(file_s));
    if (file == NULL) {
        perr("File %s was impossible to allocate.\n", (strrchr(path, '/') + 1));
        return NULL;
    }
    file->path = str_create(path);  //creo una copia del path per semplicità
    file->content = 0;
    file->size = 0;
    file->pidlist = list_create();
    file->file_lock = malloc(sizeof(pthread_mutex_t));
    file->file_cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init (file->file_lock, NULL);
    pthread_cond_init (file->file_cond, NULL);
    file->locked_by = NULL;
    return file;
}

/* Ritorna true se il file f è aperto dal Client pid, false altrimenti.
 * */
bool file_is_opened_by(file_s *f, char *pid) {
    return list_contains_key(f->pidlist, pid);
}

/* Ritorna true se il file f è vuoto, false altrimenti.
 * */
bool file_is_empty(file_s *f) {
    return f->content == NULL;
}

/* Funzione che aggiorna il contenuto e la grandezza di un file.
 * Il vecchio contenuto viene rimosso.
 * */
void file_update(file_s **f, void *newContent, size_t newSize) {
    if (newSize == 0) {   //se il file è vuoto
        free(newContent);
        return;
    }
    free((*f)->content);

    pthread_mutex_lock(&server_data_lock);
    fprintf(stdout, "%d", storage_left);
    storage_left += (*f)->size;
    storage_left -= newSize;
    fprintf(stdout, " - %d = %d \n", newSize, storage_left);
    pthread_mutex_unlock(&server_data_lock);

    (*f)->content = newContent;
    (*f)->size = newSize;

}

/* Funzione che simula la chiusura del file f da parte del Client cpid.
 * */
void client_closes_file(file_s **f, char *cpid) {

  list_remove(&(*f)->pidlist, cpid, NULL);

  int *n = (int *) hash_getValue(tbl_has_opened, cpid);
  *n -= 1;
  hash_updateValue(&tbl_has_opened, cpid, n, NULL);

  assert((*(int *) hash_getValue(tbl_has_opened, cpid)) >= 0);
}


void clear_openedFiles(char *key, void *value, bool *exit, void *cpid) {

    file_s *f = (file_s *) value;

    if (file_is_opened_by(f, (char *) cpid)) {

      if(!hash_containsKey(tbl_file_path, f->path)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
        if(config.PRINT_LOG == 2)
            pwarn("Worker obtained lock, but file was removed\n");
        if(config.PRINT_LOG == 1)
            perr("request not executed\n");
        return;
      }

      client_closes_file(&f, (char *) cpid);

      if(f->locked_by != NULL && strcmp(f->locked_by, cpid) == 0){
        free(f->locked_by);
        f->locked_by = NULL;
      }

    }

    //per togliere il warning "parameter never used"
    key = key;
    exit = exit;
}

void print_statistics() {
  if (!hash_isEmpty(tbl_file_path)) {
    pcolor(CYAN, "=== STORED FILES =============================================\n");
    hash_iterate(tbl_file_path, &print_files, NULL);
  } else {
    pcolor(CYAN, "Server is empty.\n");
  }

  pcolor(CYAN, "\n=== SETTINGS =================================================\n");
  settings_print(config);

  pcolor(CYAN, "\n=== STATISTICS ===============================================\n");
  printf("1. Number of stored files: %lu\n", (config.MAX_STORABLE_FILES - storable_files_left));
  printf("2. Storage left (Kbytes): %lu\n", (storage_left/1024));
  printf("3. Cache replacements: %zu\n\n", n_cache_replacements);



  //per rimuovere i warning
  psucc("");
  pcode(0,NULL);
}

void print_files(char *key, void *value, bool *exit, void *argv) {
  file_s* f=(file_s*) value;
  printf("[");
  if(f->size==0){
    pcolor(RED, "X");
  } else {
    pcolor(GREEN, "%zu", f->size);
  }
  printf("] ");

  pcolor(MAGENTA, "%s: ", (strrchr(key, '/') + 1));   //stampo il nome del file colorato
  printf("%s\n", key);    //stampo il suo path

  //per togliere il warning "parameter never used"
  exit=exit;
  argv=argv;
}
