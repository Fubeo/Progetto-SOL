#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
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
#include "./lib/customlog.h"

#define MASTER_WAKEUP_SECONDS 3
#define MASTER_WAKEUP_MS 0

// ======== struct necessaria per gestione dei file da parte del server ========
typedef struct {
    char *path;
    void *content;
    size_t size;
    list *pidlist;

    pthread_cond_t *file_cond;
    pthread_mutex_t *file_mtx;
    char* locked_by;
} file_s;

// =============================================================================
//                         Dichiarazione variabili e struct
// =============================================================================

// =========================== Configurazione server ===========================
bool server_running =  true;
settings config = DEFAULT_SETTINGS;
bool soft_close = false;
pthread_mutex_t server_data_mtx = PTHREAD_MUTEX_INITIALIZER;
logfile *lf;

// ========================== Gestione master-workers ==========================
queue *queue_clients;
int n_clients_connected = 0;
int pipe_fd[2];

// ========================= Gestione storage interno ==========================
list *storage_fifo;
size_t storable_files_left;
size_t storage_left;
hash_table *tbl_file_path;
hash_table *tbl_has_opened;
size_t n_cache_replacements = 0;

// ============================== Statistiche ==================================
size_t lowest_storable_files_left;
size_t lowest_storage_left;
size_t max_n_clients_connected;


//==============================================================================
//                             Dichiarazione funzioni
// =============================================================================

// ===================== Gestione funzioni base del server =====================
void init_server(int argc, char *argv[]);
void *worker_function();
void closeConnection(int fd_client, char *cpid);
void *stop_server(void *argv);
void close_server();
void print_filenumperc();

// ======================== Gestione richieste client ==========================
void writeFile(int fd_client, char *request, file_s *f);
void createFile(int fd_client, char *request);
void openFile(int fd_client, char *request);
void openO_LOCK(int fd_client, char *request);
void removeFile(int fd_client, char *request);
void closeFile(int fd_client, char *request);
void readFile(int fd_client, char *request);
void readNFiles(int fd_client, char *request);
void sendNFiles(char *key, void *value, bool *exit, void *args);
void appendFile(int fd_client, char *request);
void lockFile(int fd_client, char *request);
void unlockFile(int fd_client, char *request);

// ====================== Gestione storage del server ==========================
void free_space(int fd_client, char option, size_t fsize, char *cpid);
void file_destroy(void *f);
void file_destroy_completely(void *f);
void file_open(file_s **f, char *cpid);
bool file_isOpened(file_s *f);
static file_s *file_init(char *path);
bool file_is_opened_by(file_s *f, char *pid);
bool file_is_empty(file_s *f);
void file_update(file_s **f, void *newContent, size_t newSize);
void client_closes_file(file_s **f, char *cpid);
void clear_openedFiles(char *key, void *value, bool *exit, void *cpid);
void clear_openers(file_s *f);

// =================== Output informazioni sul server ==========================
void print_statistics();
void print_files(char *key, void *value, bool *exit, void *argv);
void print_storage_percentages();
void print_deleteddataperc(size_t deleted, int howmany);
void print_occupiedstorageperc();

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
  if(config.PRINT_LOG == 2) psucc("Ready for new connections\n");

  // creazione e inizializzazione thread pool
  pthread_t tid = 0;
  pthread_t thread_pool[config.N_WORKERS];

  for (int i = 0; i < config.N_WORKERS; i++) {
      pthread_create(&tid, NULL, &worker_function, NULL);
      thread_pool[i] = tid;
  }

  // Creazione di un thread per la gestione dei segnali
  pthread_attr_t thattr = {0};
  pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&tid, &thattr, &stop_server, NULL) != 0) {
      fprintf(stderr, "Errore: unable to start signal handler\n");
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
  if(config.PRINT_LOG > 0) pcolor(CYAN,"=== SERVER LISTENING ============================================================================\n\n");

  while (server_running) {
      fd_set ready_sockets = current_sockets;


      struct timeval tv = {MASTER_WAKEUP_SECONDS, MASTER_WAKEUP_MS};
      if ((sreturn = select(sortedlist_getMax(fd_list) + 1, &ready_sockets, NULL, NULL, &tv)) < 0) {
          if (errno != EINTR) {
              fprintf(stderr, "Select Error: value < 0\n"
                              "Error code: %s\n", strerror(errno));
          }
          server_running = false;
          break;
      }

      if (soft_close && n_clients_connected == 0) {
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
                  log_adderror(lf, "", "Client refused");
                  sendInteger(fd_client, CONNECTION_REFUSED);
                  close(fd_client);
                  break;
                }
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
                pthread_mutex_lock(&server_data_mtx);
                n_clients_connected++;
                pthread_mutex_unlock(&server_data_mtx);
                if(config.PRINT_LOG > 0){
                  psucc("A new client connected!\n");
                  printf("Number of connected clients: %d\n", n_clients_connected);
                }
                log_addline(lf, "A new client connected");
                if(max_n_clients_connected < n_clients_connected) max_n_clients_connected = n_clients_connected;
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

  if(config.PRINT_LOG > 0) print_statistics();

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
              fprintf(stderr, "Config file not found!\n"
                              "Default settings will be used\n");
          }
      }
  } else if (argc > 2) {
      pwarn("WARNING: invalid -c arguments\n");
  }
  // lettura file config
  settings_load(&config, config_path);
  free(config_path);

  // inizializzazione file di logs
  lf = log_init(config.LOGS_PATH);

  // inizializzazione statistiche
  lowest_storable_files_left = config.MAX_STORABLE_FILES;
  lowest_storage_left = config.MAX_STORAGE;
  max_n_clients_connected = 0;

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
    if(config.PRINT_LOG > 0)
      fprintf(stderr, "MAX_STORAGE cannot be higher than INT_MAX\n");
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

    fflush(stdout);fflush(lf->file);
    fprintf(stdout, "%d received: %s\n", gettid(), request);
    log_addrequest(lf, request);

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

        case 'r': {
          char *cmd;
          if (request[1] == 'n') {
            cmd = str_cut(request, 3, str_length(request) - 3);
            readNFiles(fd_client, cmd);
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
    if(config.PRINT_LOG > 0) fprintf(stdout, "\n");
  }
  return NULL;
}

void closeConnection(int fd_client, char *cpid) {
    int nfiles = *((int *) hash_getValue(tbl_has_opened, cpid));

    log_addreadablerequest(lf, "closeConnection", cpid, fd_client);

    if (nfiles == 0) {
        sendInteger(fd_client, S_SUCCESS);
    } else {

        sendInteger(fd_client, SFILES_FOUND_ON_EXIT);
        hash_iterate(tbl_file_path, &clear_openedFiles, (void *) cpid);

    }
    assert((*((int *) hash_getValue(tbl_has_opened, cpid))) == 0);

    hash_deleteKey(&tbl_has_opened, cpid, &free);
    if (close(fd_client) != 0) {

        perr("WARNING: error closing socket with client %d\n", fd_client);
        log_addcloseconnection(lf, cpid);
        n_clients_connected--;
    } else {
      log_addcloseconnection(lf, cpid);
      n_clients_connected--;
      if(config.PRINT_LOG > 0) {
        psucc("Client %s disconnected\n", cpid);
        printf("Number of connected clients: %d\n", n_clients_connected);
      }
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


    pthread_sigmask(SIG_SETMASK, &set, NULL);

    if(config.PRINT_LOG == 2) psucc("SIGWAIT Thread avviato\n");
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
    log_free(lf);
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

  log_addreadablerequest(lf, "create", cpid, fd_client);

  if (hash_containsKey(tbl_file_path, filepath)) {
    pwarn("Client %d tried to create file %s, that already exists on the server\n", fd_client, (strrchr(filepath,'/')+1));
    perr("Request denied\n");
    sendInteger(fd_client, SFILE_ALREADY_EXIST);
    log_adderror(lf, cpid, "Client tried to create a file that already exists on the server");

  } else if (fsize > config.MAX_STORAGE) { //se il file è troppo grande
    pwarn("Client %d tried to store a too large file\n", fd_client);
    perr("Request denied\n");
    sendInteger(fd_client, SFILE_TOO_LARGE);
    log_adderror(lf, cpid, "Client tried to create a too large file");
  } else {
    assert(hash_containsKey(tbl_has_opened, cpid));

    file_s *f = file_init(filepath); //creo un nuovo file

    if (f == NULL) {
      sendInteger(fd_client, MALLOC_ERROR);
      return;
    }

    // Acquisizione della lock
    pthread_mutex_lock(f->file_mtx);

    list_insert(&f->pidlist, cpid, NULL);   // aggiungo il pid del client alla lista degli openers
    sendInteger(fd_client, S_SUCCESS);			// notifico il client dell'esito positivo dell'operazione
    log_addcreate(lf, cpid, filepath);

    // scrittura del file
    char* r = receiveStr(fd_client);
    writeFile(fd_client, r, f);
    free(r);

  }
  str_clearArray(&split, n);
}

void openFile(int fd_client, char *request) {
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];  // path del file inviato dal Client
  char *cpid = array[1];      // pid del client

  log_addreadablerequest(lf, "open", cpid, fd_client);


  if (!hash_containsKey(tbl_file_path, filepath)) { // Controllo se il file non è presente nello storage
    if(config.PRINT_LOG == 2)
      pwarn("Client %s tried to open a non-existent file\n", cpid);
    if(config.PRINT_LOG == 1)
      perr("Request denied");

    sendInteger(fd_client, SFILE_NOT_FOUND);
    log_adderror(lf, cpid, "Client tried to open a non-existent file");
    str_clearArray(&array, n);
    return;
  }

  file_s *f = hash_getValue(tbl_file_path, filepath);
  if (file_is_opened_by(f, cpid)) { // Controllo se il file è già stato aperto dal Client
    sendInteger(fd_client, SFILE_ALREADY_OPENED);
    str_clearArray(&array, n);
    return;
  }

  // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
  bool still_exists;
  pthread_cond_t *cond = f->file_cond;
  pthread_mutex_t *mtx = f->file_mtx;

  // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
  // non esce dal ciclo finche' la lock non viene rilasciata.
  while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    fprintf(stdout, "SONO IL CLIENT %s, MI METTO IN ATTESA DELLA LOCK DEL FILE %s LOCKATO DA %s\n", cpid, f->path, f->locked_by);
    if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
  }
  if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
    if(config.PRINT_LOG == 2)
        pwarn("Client %s obtained lock, but file was removed\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");
    sendInteger(fd_client, SFILE_WAS_REMOVED);
    log_adderror(lf, cpid, "Client obtained lock, but file was removed");
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  file_open(&f, cpid);


  pthread_mutex_unlock(mtx);
  pthread_cond_signal(cond);

  sendInteger(fd_client, S_SUCCESS);

  log_addopen(lf, cpid, filepath);


  if(config.PRINT_LOG == 2)
    psucc("Client %s opened file %s\n", cpid, (strrchr(filepath,'/')+1));

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
    log_addreadablerequest(lf, "openlock", cpid, fd_client);

    file_s *f = hash_getValue(tbl_file_path, filepath);
    if (file_is_opened_by(f, cpid)) { // controllo se il file è già stato aperto dal Client
      if (config.PRINT_LOG == 2)
        pwarn("Client %s tried to open a file that he has already opened\n", cpid);
      if (config.PRINT_LOG == 1)
        perr("Request denied\n");

      sendInteger(fd_client, SFILE_ALREADY_OPENED);
      log_adderror(lf, cpid, "Client tried to open a file that he has already opened");
      str_clearArray(&array, n);
      free(cpid);
      return;
    }


    if(f->locked_by != NULL){
      if(config.PRINT_LOG == 2) fprintf(stdout, "Client %s waiting to open file %s in locked mode\n", cpid, f->path);
    }

    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    bool still_exists;
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_mtx;

    // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
    // non esce dal ciclo finche' la lock non viene rilasciata.
    while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
      fprintf(stdout, "SONO IL CLIENT %s, MI METTO IN ATTESA DELLA LOCK DEL FILE %s LOCKATO DA %s\n", cpid, f->path, f->locked_by);
      if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
    }
    if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client %s obtained lock, but file was removed\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("Request denied\n");
      sendInteger(fd_client, SFILE_WAS_REMOVED);
      log_adderror(lf, cpid, "Client obtained lock, but file was removed");
      str_clearArray(&array, n);
      free(cpid);
      return;
    }

    f->locked_by = cpid;
    file_open(&f, cpid);

    pthread_mutex_unlock(mtx);
    pthread_cond_signal(cond);

    sendInteger(fd_client, S_SUCCESS);
    log_addopenlock(lf, cpid, filepath);

    if (config.PRINT_LOG > 0) {
      psucc("Client %s opened file %s in locked mode\n", cpid, (strrchr(filepath,'/') + 1));
    }

    str_clearArray(&array, n);

  } else {                                        // Se, invece, il file non e' presente nel server
    sendInteger(fd_client, SFILE_NOT_FOUND);
    log_addreadablerequest(lf, "createlocked", cpid, fd_client);
    int response = receiveInteger(fd_client);
    if(response == SFILE_NOT_FOUND){
        if (config.PRINT_LOG == 2){
            pwarn("Client %s tried to create a file that does not exist\n", cpid);
        }
        if (config.PRINT_LOG == 1){
            perr("Request denied\n");
        }
        log_adderror(lf, cpid, "Client tried to create a file that does not exist");
        str_clearArray(&array, n);
        free(cpid);
        return;
    }
    size_t fsize = receiveInteger(fd_client);

    if (fsize > config.MAX_STORAGE) {             // Se il file è troppo grande
      pwarn("Client %s tried to write a too large file\n", cpid);
      perr("Request denied\n");
      sendInteger(fd_client, SFILE_TOO_LARGE);
      log_adderror(lf, cpid, "Client tried to write a too large file");
      free(cpid);
    } else {
      assert(hash_containsKey(tbl_has_opened, cpid));

      file_s *f = file_init(filepath);//creo un nuovo file

      if (f == NULL) {
        sendInteger(fd_client, MALLOC_ERROR);
        str_clearArray(&array, n);
        free(cpid);
        return;
      }

      // Acquisizione della lock
      pthread_mutex_lock(f->file_mtx);

      list_insert(&f->pidlist, cpid, NULL);   // aggiungo il pid del client alla lista degli openers
      f->locked_by = cpid;                    // assegno o_lock al client

      sendInteger(fd_client, S_SUCCESS);			// notifico il client dell'esito positivo dell'operazione
      log_addcreatelock(lf, cpid, filepath);

      char* req = receiveStr(fd_client);
      writeFile(fd_client, req, f);
      free(req);
    }
    str_clearArray(&array, n);
  }

}

void removeFile(int fd_client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":");
    char *filepath = array[0];  // path del file inviato dal Client
    char *cpid = array[1];      // pid del client

    // Controllo se il file esiste
    if (!hash_containsKey(tbl_file_path, filepath)) {
            pwarn("Client %s tried to access a non-existent file\n", cpid);


        sendInteger(fd_client, SFILE_NOT_FOUND);
        log_adderror(lf, cpid, "Client tried to access a non-existent file");
        str_clearArray(&array, n);
        return;
    }


    file_s *f = hash_getValue(tbl_file_path, filepath);


    // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
    bool still_exists;
    pthread_cond_t *cond = f->file_cond;
    pthread_mutex_t *mtx = f->file_mtx;

    // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
    // non esce dal ciclo finche' la lock non viene rilasciata.
    while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
      if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
    }
    if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
      if(config.PRINT_LOG == 2)
          pwarn("Client %s obtained lock, but file was removed\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("Request denied\n");
      sendInteger(fd_client, SFILE_WAS_REMOVED);
      log_adderror(lf, cpid, "Client obtained lock, but file was removed");
      str_clearArray(&array, n);
      return;
    }

    if(config.PRINT_LOG == 2)
      print_deleteddataperc(f->size, 1);

    // Aggiorno i dati del server
    pthread_mutex_lock(&server_data_mtx);
    storable_files_left++;
    storage_left += f->size;
    if (storage_left > config.MAX_STORAGE) {
      storage_left = config.MAX_STORAGE;
    }
    pthread_mutex_unlock(&server_data_mtx);

    // Rimozione del file dallo storage
    clear_openers(f);
    log_addremove(lf, cpid, f->path, f->size);
    hash_deleteKey(&tbl_file_path, filepath, &file_destroy);

    pthread_mutex_unlock(mtx);
    pthread_cond_signal(cond);

    // Libero la memoria dai mutex dei file
    pthread_cond_destroy(cond);
    pthread_mutex_destroy(mtx);
    free(cond);
    free(mtx);

    sendInteger(fd_client, S_SUCCESS);

    if(config.PRINT_LOG == 2){
      psucc("Client %s removed file %s\n", cpid, (strrchr(filepath,'/')+1));
    }

    str_clearArray(&array, n);
}

void closeFile(int fd_client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":");
    assert(n == 2);

    char *filepath = array[0];
    char *cpid = array[1];

    log_addreadablerequest(lf, "close", cpid, fd_client);

    if (!hash_containsKey(tbl_file_path, filepath)) {
      if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to close a non-existent file\n", cpid);
      if(config.PRINT_LOG == 1)
        pwarn("Request denied\n");
      log_adderror(lf, cpid, "Client tried to close a non-existent file");

      sendInteger(fd_client, SFILE_NOT_FOUND);
      str_clearArray(&array, n);
      return;
    }


    file_s *f = hash_getValue(tbl_file_path, filepath);

    if (!file_is_opened_by(f, cpid)) {
      if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to close a a file that he did not open: %s\n", cpid, (strrchr(filepath,'/')+1));
      if(config.PRINT_LOG == 1)
        pwarn("Request denied\n");

      sendInteger(fd_client, SFILE_NOT_OPENED);
      log_adderror(lf, cpid, "Client tried to close a a file that he did not open:");
      str_clearArray(&array, n);
      return;
    }

    client_closes_file(&f, cpid);

    if(f->locked_by != NULL && ((strcmp(f->locked_by, cpid) == 0))){
      free(f->locked_by);
      f->locked_by = NULL;
    }

    pthread_mutex_unlock(f->file_mtx);
    pthread_cond_signal(f->file_cond);

    sendInteger(fd_client, S_SUCCESS);
    log_addclose(lf, cpid, filepath);

    if(config.PRINT_LOG > 0)
      psucc("File %s closed by client %s\n", (strrchr(filepath,'/')+1), cpid);

    str_clearArray(&array, n);
}

void writeFile(int fd_client, char *request, file_s *f) {
  char **split = NULL;
  int n = str_split(&split, request, ":?");
  assert(n == 3);
  char *filepath = split[0];
  char *cpid = split[1];

  char option = (split[2])[0];
  assert(option == 'y' || option == 'n');

  log_addreadablerequest(lf, "write", cpid, fd_client);

  size_t fsize;

  void *fcontent = NULL;

  // Il server riceve il contenuto e la dimensione del file
  receivefile(fd_client, &fcontent, &fsize);


  if (fsize > config.MAX_STORAGE) {
    pwarn("Client %s sent a too large file\n", cpid);
    sendInteger(fd_client, SFILE_TOO_LARGE);
    log_adderror(lf, cpid, "Client sent a too large file");
    free(fcontent);
    str_clearArray(&split, n);
    return;
  }

  if (!file_is_opened_by(f, cpid)) {
    pwarn("Client %s tried to write a file that he did not open: %s\n", cpid, (strrchr(filepath,'/')+1));

    sendInteger(fd_client, SFILE_NOT_OPENED);
    log_adderror(lf, cpid, "Client tried to write a file that he did not open");
    free(fcontent);
    str_clearArray(&split, n);
    return;
  } else if (!file_is_empty(f)) {
    pwarn("Client %s tried to write a not empty file\n", cpid);


    sendInteger(fd_client, SFILE_NOT_EMPTY);
    log_adderror(lf, cpid, "Client tried to write a not empty file");
    free(fcontent);
    str_clearArray(&split, n);
    return;
  }

  //da qui in poi il file viene inserito
  if (fsize > storage_left || storable_files_left == 0) {  //se non ho spazio
    if(config.PRINT_LOG == 2)
      pwarn("Capacity miss detected: not enough storage space left\n");

    sendInteger(fd_client, S_STORAGE_FULL);
    free_space(fd_client, option, fsize, cpid);

    if (fsize > storage_left || storable_files_left == 0) {
        if (config.PRINT_LOG > 1)
          perr("Server was unable to free storage for the new file\n");

      free(fcontent);
      str_clearArray(&split, n);
      sendInteger(fd_client, S_FREE_ERROR);
      log_adderror(lf, cpid, "Server was unable to free storage for the new file");

      return;
    }
    if(config.PRINT_LOG > 0)
      psucc("Storage freed for file %s added by %s\n", (strrchr(filepath,'/')+1), cpid);

    char* s = str_concatn("Storage freed for file ", (strrchr(filepath,'/')+1), " added by ", cpid, NULL);
    log_addline(lf, s);
    free(s);
  }

  // Scrivo il contenuto nel file
  assert(f->path != NULL && !str_is_empty(f->path));
  assert(storage_left <= config.MAX_STORAGE);


  hash_insert(&tbl_file_path, filepath, f);
  file_update(&f, fcontent, fsize);

  log_addwrite(lf, cpid, filepath, fsize);
  pthread_mutex_lock(&server_data_mtx);
  storable_files_left--;										// aggiorno il numero di file memorizzabili
  if(lowest_storable_files_left > storable_files_left) lowest_storable_files_left = storable_files_left;
  if(lowest_storage_left > storage_left) lowest_storage_left = storage_left;
  pthread_mutex_unlock(&server_data_mtx);

  list_insert(&storage_fifo, filepath, f);  // e infine lo aggiungo alla coda fifo
  int *a = (int *) hash_getValue(tbl_has_opened, cpid);
  *a = *a + 1;

  hash_updateValue(&tbl_has_opened, cpid, a, NULL);

  sendInteger(fd_client, S_SUCCESS);

  // Rilascio la lock

  pthread_mutex_unlock(f->file_mtx);
  pthread_cond_signal(f->file_cond);

  if(config.PRINT_LOG > 0){
    psucc("Client %s has written file %s of size %ld\n", cpid, (strrchr(filepath,'/')+1), fsize);
    if(config.PRINT_LOG == 2) print_storage_percentages(storage_left, config.MAX_STORAGE);
  }
  str_clearArray(&split, n);

}

void readFile(int fd_client, char *request) {
  assert(!str_is_empty(request) && request != NULL);
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];
  char *cpid = array[1];

  log_addreadablerequest(lf, "read", cpid, fd_client);

  //controllo che il file sia stato creato
  if (!hash_containsKey(tbl_file_path, filepath)) {
      if(config.PRINT_LOG == 2)
          pwarn("Client %s tried to read a non-existant file\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("Request denied\n");
      sendInteger(fd_client, SFILE_NOT_FOUND);
      log_adderror(lf, cpid, "Client tried to read a non-existant file");
      str_clearArray(&array, n);
      return;
  }
  file_s *f = hash_getValue(tbl_file_path, filepath);

  if(!file_is_opened_by(f, cpid)){
    if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to read a not opened file\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");

      sendInteger(fd_client, SFILE_NOT_OPENED);
      log_adderror(lf, cpid, "Client tried to read a not opened file");
      str_clearArray(&array, n);
      return;
  }

  // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
  bool still_exists;
  pthread_cond_t *cond = f->file_cond;
  pthread_mutex_t *mtx = f->file_mtx;

  // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
  // non esce dal ciclo finche' la lock non viene rilasciata.
  while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
  }
  if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
    if(config.PRINT_LOG == 2)
        pwarn("Client %s obtained lock, but file was removed\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");
    sendInteger(fd_client, SFILE_WAS_REMOVED);
    log_adderror(lf, cpid, "Client obtained lock, but file was removed");
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  sendInteger(fd_client, S_SUCCESS);

  sendn(fd_client, f->content, f->size);

  if(config.PRINT_LOG > 0){
    pcolor(GREEN, "Client %s has read file ", cpid);
    pcolor(STANDARD, "%s\n", (strrchr(f->path,'/')+1));
  }

  log_addread(lf, cpid, filepath, f->size);

  if(f->locked_by == NULL || strcmp(f->locked_by, cpid) != 0) {
    pthread_mutex_unlock(mtx);
    pthread_cond_signal(cond);
  }

  str_clearArray(&array, n);
}

void readNFiles(int fd_client, char *request) {
  assert(!str_is_empty(request) && request != NULL);
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *nf = array[0];
  char *cpid = array[1];

  log_addreadablerequest(lf, "readn", cpid, fd_client);

  if (hash_isEmpty(tbl_file_path)) {
      if(config.PRINT_LOG == 2)
          pwarn("Client %s requested a readn, but storage is empty\n", cpid);
      if(config.PRINT_LOG == 1)
          perr("Request denied\n");
      sendInteger(fd_client, S_STORAGE_EMPTY);
      log_adderror(lf, cpid, "Client requested a readn, but storage is empty");
      str_clearArray(&array, n);
      return;
  }

  int n_files_to_send;
  int ret = str_toInteger(&n_files_to_send, nf);
  assert(ret != -1);

  list_node *params = malloc(sizeof(list_node));
  params->key = (void *) &fd_client;
  params->value = (void *) cpid;

  if(config.PRINT_LOG > 0){
    if(n_files_to_send) pcolor(CYAN, "Client %s starts to read %d files\n", cpid, n_files_to_send);
    else pcolor(CYAN, "Client %s starts to read all stored files (except if locked by someone else)\n", cpid);
  }

  sendInteger(fd_client, S_SUCCESS);
  hash_iteraten(tbl_file_path, &sendNFiles, (void *) params, n_files_to_send);
  sendInteger(fd_client, EOS_F);

  str_clearArray(&array, n);
  free(params);
}

void sendNFiles(char *key, void *value, bool *exit, void *args) {

  list_node *params = (list_node*) args;

  int fd_client = *((int*) params->key);
  char *cpid = (char*) params->value;

  file_s *f = (file_s *) value;

  if(f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0) {
    if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to read a locked file\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");
    log_adderror(lf, cpid, "Client tried to read a locked file");
    return;
  }
  sendInteger(fd_client, !EOS_F);
  sendStr(fd_client, key);
  sendn(fd_client, f->content, f->size);
  if(config.PRINT_LOG > 0){
    pcolor(GREEN, "Client %s has read file ", cpid);
    pcolor(STANDARD, "%s\n", (strrchr(f->path,'/')+1));
  }
  log_addread(lf, cpid, f->path, f->size);

  //per togliere il warning "parameter never used"
  exit=exit;
  args=args;
}

void appendFile(int fd_client, char *request) {
  char **array = NULL;
  int n = str_split(&array, request, ":?");
  char *filepath = array[0];
  char *cpid = array[1];
  char option = (array[2])[0];

  assert(option == 'y' || option == 'n');


  log_addreadablerequest(lf, "append", cpid, fd_client);

  void *fcontent;
  size_t fsize;
  receivefile(fd_client, &fcontent, &fsize);
  file_s *f = hash_getValue(tbl_file_path, filepath);

  if (!hash_containsKey(tbl_file_path, filepath)) {
    if (config.PRINT_LOG == 2)
      pwarn("Client %s tried to append data to a non-existent file\n", cpid);
    if(config.PRINT_LOG == 1)
      perr("Request denied\n");

    sendInteger(fd_client, SFILE_NOT_FOUND);
    log_adderror(lf, cpid, "Client tried to append data to a non-existent file");
    free(fcontent);
    str_clearArray(&array, n);
    return;
  }


  if ((f->size + fsize) > config.MAX_STORAGE) {
    if(config.PRINT_LOG == 2)
      pwarn("Client %s sent a too large file\n", cpid);
    if(config.PRINT_LOG == 1)
      perr("Request denied\n");

    sendInteger(fd_client, SFILE_TOO_LARGE);
    log_adderror(lf, cpid, "Client sent a too large file");
    free(fcontent);
    str_clearArray(&array, n);
    return;
  }
  fprintf(stdout, "worker %d: ARRIVO QUI MAREMMA BALDRACCA %s\n", gettid(), cpid);


  if (!file_is_opened_by(f, cpid)) {
    if (config.PRINT_LOG == 2)
      pwarn("Client %s tried to append a non-opened file\n", cpid);
    if(config.PRINT_LOG == 1)
      perr("Request denied\n");
    sendInteger(fd_client, SFILE_NOT_OPENED);
    log_adderror(lf, cpid, "Client tried to append a non-opened file");
    free(fcontent);
    str_clearArray(&array, n);
    return;
  }

  // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
  bool still_exists;
  pthread_cond_t *cond = f->file_cond;
  pthread_mutex_t *mtx = f->file_mtx;

  // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
  // non esce dal ciclo finche' la lock non viene rilasciata.
  while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
  }
  if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
    if(config.PRINT_LOG == 2)
      pwarn("Client %s obtained lock, but file was removed\n", cpid);
    if(config.PRINT_LOG == 1)
      perr("Request denied\n");
    sendInteger(fd_client, SFILE_WAS_REMOVED);
    log_adderror(lf, cpid, "Client obtained lock, but file was removed");
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  //da qui in poi il file viene inserito
  if (fsize > storage_left) {  //se non ho spazio
    if (config.PRINT_LOG == 2) {
        pwarn("Detected capacity miss\n");
    }
    sendInteger(fd_client, S_STORAGE_FULL);
    free_space(fd_client, option, fsize, cpid);

    if (fsize >= storage_left) {
        if (config.PRINT_LOG > 1)
          perr("Server was unable to free storage for the new file\n");

        free(fcontent);
        str_clearArray(&array, n);
        sendInteger(fd_client, S_FREE_ERROR);
        log_adderror(lf, cpid, "Server was unable to free storage for the new file");
        return;
    }

    if(config.PRINT_LOG == 2)
      psucc("Storage freed for file %s appended by %s\n", (strrchr(filepath,'/')+1), cpid);

    char* s = str_concatn("Storage freed for file ", (strrchr(filepath,'/')+1), " appended by ", cpid, NULL);
    log_addline(lf, s);
    free(s);

  }


  size_t newSize = f->size + fsize;
  void *newContent = malloc(newSize);
  if (newContent == NULL) {
      perr("Malloc error: unable to append the requested files\n");
      sendInteger(fd_client, MALLOC_ERROR);
      log_adderror(lf, cpid, "Unable to append the requested files");
      return;
  }

  memcpy(newContent, f->content, f->size);
  memcpy(newContent + f->size, fcontent, fsize);


  pthread_mutex_lock(&server_data_mtx);
  free(f->content);
  f->content = newContent;
  f->size = newSize;
  storage_left -= fsize;
  if(lowest_storage_left > storage_left) lowest_storage_left = storage_left;
  pthread_mutex_unlock(&server_data_mtx);


  if (config.PRINT_LOG == 2) {
    psucc("Client %s appended %d bytes to %s\n", cpid, fsize, (strrchr(f->path,'/')+1));
    print_storage_percentages();
  }

  sendInteger(fd_client, S_SUCCESS);
  log_addappend(lf, cpid, f->path, fsize);

  print_storage_percentages();


  // Rilascio la lock
  pthread_mutex_unlock(mtx);
  pthread_cond_signal(cond);
  fprintf(stdout, "worker %d: ARRIVO QUI MAREMMA BALDRACCA FUORI %s\n", gettid(), cpid);

  free(fcontent);
  str_clearArray(&array, n);
}

void lockFile(int fd_client, char *request){
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];  // path del file inviato dal Client
  char *cpid = malloc(strlen(array[1])*sizeof(char)+1);
  strcpy(cpid, array[1]);      // pid del client

  log_addreadablerequest(lf, "lock", cpid, fd_client);

  if (!hash_containsKey(tbl_file_path, filepath)) { // Se il file non e' presente nello storage
    if (config.PRINT_LOG == 2) {
      pwarn("Client %s tried to lock a file that does not exist\n", cpid);
    }
    if (config.PRINT_LOG == 1){
      perr("Request denied\n");
    }
    sendInteger(fd_client, SFILE_NOT_FOUND);
    log_adderror(lf, cpid, "Client tried to lock a file that does not exist");
    str_clearArray(&array, n);
    free(cpid);
    return;
  }


  file_s *f = hash_getValue(tbl_file_path, filepath);

  if (!file_is_opened_by(f, cpid)) { // Se il file non e' stato aperto dal client
    if (config.PRINT_LOG == 2) {
      pwarn("Client %s tried to lock a file not opened\n", cpid);
    }
    if (config.PRINT_LOG == 1){
      perr("Request denied\n");
    }
    sendInteger(fd_client, SFILE_NOT_OPENED);
    log_adderror(lf, cpid, "Client tried to lock a file not opened");
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  if(f->locked_by != NULL)
    if(config.PRINT_LOG == 2) fprintf(stdout, "Client %s waiting to lock file %s\n", cpid, f->path);

  // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
  bool still_exists;
  pthread_cond_t *cond = f->file_cond;
  pthread_mutex_t *mtx = f->file_mtx;

  // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
  // non esce dal ciclo finche' la lock non viene rilasciata.
  while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
  }
  if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
    if(config.PRINT_LOG == 2)
        pwarn("Client %s obtained lock, but file was removed\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");
    sendInteger(fd_client, SFILE_WAS_REMOVED);
    log_adderror(lf, cpid, "Client obtained lock, but file was removed");
    str_clearArray(&array, n);
    free(cpid);
    return;
  }

  bool has_opened = file_is_opened_by(f, cpid);
  if (!(has_opened)) { // Controllo se il file è già stato aperto dal Client
    file_open(&f, cpid);
  }

  f->locked_by = cpid;

  pthread_mutex_unlock(mtx);
  pthread_cond_signal(cond);

  sendInteger(fd_client, S_SUCCESS);

  log_addlock(lf, cpid, filepath);

  if(config.PRINT_LOG > 0)
    psucc("Client %s locked file %s\n", cpid, (strrchr(filepath,'/')+1));


  str_clearArray(&array, n);
}

void unlockFile(int fd_client, char *request){
  assert(!str_is_empty(request) && request != NULL);
  char **array = NULL;
  int n = str_split(&array, request, ":");
  char *filepath = array[0];
  char *cpid = array[1];

  log_addreadablerequest(lf, "unlock", cpid, fd_client);

  //controllo che il file sia stato creato
  if (!hash_containsKey(tbl_file_path, filepath)) {
    if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to unlock a non-existent file\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");

    sendInteger(fd_client, SFILE_NOT_FOUND);
    log_adderror(lf, cpid, "Client tried to unlock a non-existent file");
    str_clearArray(&array, n);
    return;
  }

  file_s *f = hash_getValue(tbl_file_path, filepath);

  if(f->locked_by == NULL){
    if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to unlock a non-locked file\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");
    log_adderror(lf, cpid, "Client tried to unlock a non-locked file");
    sendInteger(fd_client, SFILE_NOT_LOCKED);
    str_clearArray(&array, n);
    return;
  }

  if(strcmp(f->locked_by, cpid) != 0){
    if(config.PRINT_LOG == 2)
        pwarn("Client %s tried to unlock a file locked by another client\n", cpid);
    if(config.PRINT_LOG == 1)
        perr("Request denied\n");

  log_adderror(lf, cpid, "Client tried to unlock a file locked by another client");
    sendInteger(fd_client, CLIENT_NOT_ALLOWED);
    str_clearArray(&array, n);
    return;
  }

  free(f->locked_by);
  f->locked_by = NULL;

  pthread_mutex_unlock(f->file_mtx);
  pthread_cond_signal(f->file_cond);

  sendInteger(fd_client, S_SUCCESS);
  log_addunlock(lf, cpid, filepath);

  if(config.PRINT_LOG != 0)
      psucc("Client %s unlocked file %s\n", cpid, filepath);
  str_clearArray(&array, n);
}


// ====================== Gestione storage del server ==========================

// Funzione di rimozione dei file in caso di Capacity Misses.
void free_space(int fd_client, char option, size_t fsize, char *cpid) {
  list_node *curr = storage_fifo->head;

  while (true) {
    if (curr == NULL) {   //ho finito di leggere la coda
      if(config.PRINT_LOG == 2)
        psucc("Lettura coda FIFO terminata\n");
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
    if ((f->locked_by != NULL && strcmp(f->locked_by, cpid) == 0) || f->locked_by == NULL) {
      char *filepath = malloc(strlen(f->path)+1*sizeof(char));
      strcpy(filepath, f->path);


      // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
      bool still_exists;
      pthread_cond_t *cond = f->file_cond;
      pthread_mutex_t *mtx = f->file_mtx;

      // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
      // non esce dal ciclo finche' la lock non viene rilasciata.
      while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
        if (pthread_cond_wait(cond, mtx) != 0) fprintf(stderr, "Error during pthread_cond_wait\n");
      }
      if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
        if(config.PRINT_LOG == 2)
            pwarn("Client %s obtained lock, but file was removed\n", cpid);
        if(config.PRINT_LOG == 1)
            perr("Request denied\n");
        log_adderror(lf, cpid, "Client obtained lock, but file was removed");
        sendInteger(fd_client, EOS_F);
        return;
      }

      free(filepath);

      if(config.PRINT_LOG == 2)
          pcolor(CYAN, "Removing from queue file %s\n", (strrchr(f->path, '/')+1));

      if (option == 'y') {  //caso in cui il file viene espulso e inviato al client
          sendInteger(fd_client, !EOS_F);
          sendStr(fd_client, f->path);
          sendn(fd_client, f->content, f->size);
      }
      if(config.PRINT_LOG == 2)
        print_deleteddataperc(f->size, 1);
      pthread_mutex_lock(&server_data_mtx);
      storage_left += f->size;
      if (storage_left > config.MAX_STORAGE) {
          // mi assicuro di rimanere nel range 0 <= storage_left <= MA
      }
      storable_files_left++;
      n_cache_replacements++;
      log_addeject(lf, cpid, f->path, f->size);
      pthread_mutex_unlock(&server_data_mtx);

      // Rimozione del file dallo storage
      clear_openers(f);
      hash_deleteKey(&tbl_file_path, f->path, &file_destroy);

      pthread_mutex_unlock(mtx);
      pthread_cond_signal(cond);

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
  pthread_mutex_destroy(file->file_mtx);
  free(file->file_mtx);
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
    file->file_mtx = malloc(sizeof(pthread_mutex_t));
    file->file_cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init (file->file_mtx, NULL);
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

    pthread_mutex_lock(&server_data_mtx);
    storage_left += (*f)->size;
    storage_left -= newSize;
    pthread_mutex_unlock(&server_data_mtx);

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

  if((*f)->locked_by != NULL && !strcmp((*f)->locked_by, cpid)){
    pthread_cond_signal((*f)->file_cond);
    pthread_mutex_unlock((*f)->file_mtx);
  }

  assert((*(int *) hash_getValue(tbl_has_opened, cpid)) >= 0);
}

void clear_openedFiles(char *key, void *value, bool *exit, void *cpid) {

    file_s *f = (file_s *) value;

    if (file_is_opened_by(f, (char *) cpid)) {

      if(!hash_containsKey(tbl_file_path, f->path)){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
        if(config.PRINT_LOG == 2)
            pwarn("Worker obtained lock, but file was removed\n");
        if(config.PRINT_LOG == 1)
            perr("Request denied\n");
        return;
      }

      client_closes_file(&f, (char *) cpid);

      if(config.PRINT_LOG > 0){
        psucc("File ");
        pcolor(STANDARD, "%s", (strrchr(f->path,'/')+1));
        psucc(" (automatically) closed by client %s\n", cpid);
      }

      if(f->locked_by != NULL && strcmp(f->locked_by, cpid) == 0){
        free(f->locked_by);
        f->locked_by = NULL;
        pthread_mutex_unlock(f->file_mtx);
        pthread_cond_signal(f->file_cond);
      }
      log_addclose(lf, cpid, f->path);

    }

    //per togliere il warning "parameter never used"
    key = key;
    exit = exit;
}

void clear_openers(file_s *f){
  list_node *corr = f->pidlist->head;
  while(corr != NULL){
    int *x = (int *) hash_getValue(tbl_has_opened, corr->key);
    *x -= 1;
    hash_updateValue(&tbl_has_opened, corr->key, x, NULL);
    log_addclose(lf, corr->key, f->path);
    corr = corr->next;
  }
}

void print_statistics() {
  if (!hash_isEmpty(tbl_file_path)) {
    pcolor(CYAN, "=== STORED FILES =============================================================================\n");
    hash_iterate(tbl_file_path, &print_files, NULL);
  } else {
    pcolor(CYAN, "Server is empty.\n");
  }
  fprintf(stdout, "\n");
  print_storage_percentages();

  pcolor(CYAN, "\n=== SETTINGS =================================================================================\n");
  settings_print(config);

  pcolor(CYAN, "\n=== STATISTICS ===============================================================================\n");
  printf("Max number of stored files: \t\t\t\t%lu\n", (config.MAX_STORABLE_FILES - lowest_storable_files_left));
  printf("Max stored data size (Mbytes): \t\t\t\t%.3f\n", ((float)(config.MAX_STORAGE - lowest_storage_left)) / 1024 / 1024);
  printf("Cache replacements: \t\t\t\t\t%zu\n", n_cache_replacements);
  printf("Max number of simultaneously connected clients: \t%zu\n\n", max_n_clients_connected);

  // Aggiungo le statistiche al file di log
  log_addStats(
    lf,
    (config.MAX_STORAGE - lowest_storage_left) / 1024 / 1024,
    (config.MAX_STORABLE_FILES - lowest_storable_files_left),
    max_n_clients_connected
  );

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

  pcolor(BLUE, "%s: ", (strrchr(key, '/') + 1));   // stampo il nome del file colorato
  printf("%s\n", key);    //stampo il suo path

  //per togliere il warning "parameter never used"
  exit=exit;
  argv=argv;
}


void print_storage_percentages(){
  print_occupiedstorageperc();
  print_filenumperc();
}

void print_deleteddataperc(size_t deleted, int howmany){
  float occupied_storage_prc = (float)(config.MAX_STORAGE*100 - storage_left*100)/config.MAX_STORAGE;
  float deleted_storage_prc = (float)(deleted*100)/config.MAX_STORAGE;


  pcolor(CYAN, "Current storage size usage: ");
  fprintf(stdout, "%.3f%c (%ld of %ld)\t\t", occupied_storage_prc - deleted_storage_prc, '%', config.MAX_STORAGE - (storage_left + deleted), config.MAX_STORAGE);

  pcolor(RED, "Deleted data: ");
  fprintf(stdout, "%.3f%c (%ld)\n", deleted_storage_prc, '%', deleted);

  if(1000*(occupied_storage_prc - ((int)occupied_storage_prc)) > 500) occupied_storage_prc = ((int)occupied_storage_prc) + 1;
  else occupied_storage_prc = ((int)occupied_storage_prc);

  if(1000*(deleted_storage_prc - ((int)deleted_storage_prc)) > 500) deleted_storage_prc = ((int)deleted_storage_prc) + 1;
  else deleted_storage_prc = (int)deleted_storage_prc;

  for(int i=0; i < occupied_storage_prc - deleted_storage_prc; i++){ pcolor(CYAN, "■"); }
  for(int i=0; i < deleted_storage_prc; i++){ pcolor(RED, "■"); }
  for(int i=0; i < 100 - (occupied_storage_prc - deleted_storage_prc + deleted_storage_prc); i++){ pcolor(WHITE, "■"); }
  fprintf(stdout, "\n");

  float nstored_files_prc = (float)((config.MAX_STORABLE_FILES*100 - (storable_files_left+1)*100))/config.MAX_STORABLE_FILES;
  float removed_files_prc = (float)(1*100)/config.MAX_STORABLE_FILES;

  pcolor(GREEN, "Current files stored: ");
  fprintf(stdout, "%.3f %c (%ld of %d)\n", nstored_files_prc, '%', config.MAX_STORABLE_FILES - storable_files_left - howmany, config.MAX_STORABLE_FILES);

  if(nstored_files_prc - ((int)nstored_files_prc) > 0.5) nstored_files_prc = ((int)nstored_files_prc) + 1;
  for(int i=0; i < ((int) nstored_files_prc); i++){ pcolor(GREEN, "■"); }
  for(int i=0; i < ((int)removed_files_prc); i++){ pcolor(RED, "■"); }
  for(int i=0; i < 100 - ((int) nstored_files_prc + removed_files_prc); i++){ pcolor(WHITE, "■"); }
  fprintf(stdout, "\n\n");
}

void print_occupiedstorageperc(){
  float occupied_storage_prc = (float)((config.MAX_STORAGE*100 - storage_left*100))/config.MAX_STORAGE;
  pcolor(CYAN, "Current storage size usage: ");
  fprintf(stdout, "%.3f%c (%ld of %ld)\n", occupied_storage_prc, '%', config.MAX_STORAGE - storage_left, config.MAX_STORAGE);

  if(1000*(occupied_storage_prc - ((int)occupied_storage_prc)) > 500) occupied_storage_prc = ((int)occupied_storage_prc) + 1;
  else occupied_storage_prc = ((int)occupied_storage_prc);

  for(int i=0; i < ((int)occupied_storage_prc); i++){ pcolor(CYAN, "■"); }
  for(int i=0; i < 100 - ((int)occupied_storage_prc); i++){ pcolor(WHITE, "■"); }
  fprintf(stdout, "\n");
}

void print_filenumperc(){
  float nstored_files_prc = (float)((config.MAX_STORABLE_FILES*100 - storable_files_left*100))/config.MAX_STORABLE_FILES;

  pcolor(GREEN, "Current files stored: ");
  fprintf(stdout, "%.3f %c (%ld of %d)\n", nstored_files_prc, '%', config.MAX_STORABLE_FILES - storable_files_left, config.MAX_STORABLE_FILES);

  if(nstored_files_prc - ((int)nstored_files_prc) > 0.5) nstored_files_prc = ((int)nstored_files_prc) + 1;
  for(int i=0; i < ((int) nstored_files_prc); i++){ pcolor(GREEN, "■"); }
  for(int i=0; i < 100 - ((int) nstored_files_prc); i++){ pcolor(WHITE, "■"); }
  fprintf(stdout, "\n");
}
