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
void closeConnection(int client, char *cpid);
void *stop_server(void *argv);

// Gestione richieste fd_sk_client
void write_file(int fd_sk_client, char *request);
void createFile(int fd_sk_client, char *request);
void closeFile(int fd_sk_client, char *request);

void free_space(int fd_sk_client, char option, size_t fsize);
void file_destroy(void *f);
void file_open(file_s **f, char *cpid);
bool file_isOpened(file_s *f);
static file_s *file_init(char *path);
void clear_openedFiles(char *key, void *value, bool *exit, void *cpid);
void print_storage();
bool file_is_opened_by(file_s *f, char *pid);
bool file_is_empty(file_s *f);
void file_update(file_s **f, void *newContent, size_t newSize);
void client_closes_file(file_s **f, char *cpid);

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
          case 'w': {
            char *cmd = str_cut(request, 2, str_length(request) - 2);
            fprintf(stdout, "%s\n", cmd);
            write_file(fd_sk_client, cmd);
            free(cmd);
            break;
          }
          case 'c': {
            char *cmd;
            if (request[1] == 'l') {
              cmd = str_cut(request, 3, str_length(request) - 3);
              closeFile(fd_sk_client, cmd);
              free(cmd);
            } else {
              cmd = str_cut(request, 2, str_length(request) - 2);
              createFile(fd_sk_client, cmd);
              free(cmd);
            }
            break;
          }
          case 'e': {
            char *cmd = str_cut(request, 2, str_length(request) - 2);
            closeConnection(fd_sk_client, cmd);
            free(cmd);
            break;
          }
        }
        print_storage();
      }
      if (request[0] != 'e' || str_is_empty(request)) {
        if (writen(pipe_fd[1], &fd_sk_client, sizeof(int)) == -1) {
          fprintf(stderr, "An error occurred on write back client to the pipe\n");
          exit(errno);
        }
      }

      free(request);

      sleep(3);

    }

}

void closeConnection(int client, char *cpid) {
    int nfiles = *((int *) hash_getValue(tbl_has_opened, cpid));

      fprintf(stdout, "NFILES: %d\n", nfiles);
    if (nfiles == 0) {
        sendInteger(client, S_SUCCESS);
    } else {

            pwarn("ATTENZIONE: il Client %d non ha chiuso dei file\n"
                  "Chiusura in corso...", client);


        sendInteger(client, SFILES_FOUND_ON_EXIT);
        hash_iterate(tbl_file_path, &clear_openedFiles, (void *) cpid);

            printf("Chiusura completata!\n");

    }

    assert((*((int *) hash_getValue(tbl_has_opened, cpid))) == 0);

    hash_deleteKey(&tbl_has_opened, cpid, &free);
    if (close(client) != 0) {
        perr("ATTENZIONE: errore nella chiusura del Socket con il client %d\n", client);
    } else psucc("Client %d disconnesso\n\n", client);

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

void write_file(int fd_sk_client, char *request) {
    char **split = NULL;
    int n = str_split(&split, request, ":?");
    assert(n == 3);
    char *filepath = split[0];
    char *cpid = split[1];

    int uno;

    char option = (split[2])[0];
    assert(option == 'y' || option == 'n');
    size_t fsize;

    void *fcontent = NULL;

    // Il server riceve il contenuto e la dimensione del file
    receivefile(fd_sk_client, &fcontent, &fsize);
    fprintf(stdout, "file_content at addr %p:\n%s\n", fcontent, fcontent);
    fprintf(stdout, "File size: %d\n\n", fsize);



    if (fsize > config.MAX_STORAGE) {
        pwarn("Il fd_sk_client %d ha inviato un file troppo grande\n", fd_sk_client);

        sendInteger(fd_sk_client, SFILE_TOO_LARGE);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    }

    if (!hash_containsKey(tbl_file_path, filepath)) {

    		fprintf(stdout, "FILE CHE NON ESISTE:\n%s/\n", filepath);

        pwarn("Il client %d ha eseguito un'operazione su un file che non esiste\n", fd_sk_client);


        sendInteger(fd_sk_client, SFILE_NOT_FOUND);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    }

    file_s *f = hash_getValue(tbl_file_path, filepath);
    if (!file_is_opened_by(f, cpid)) {
        pwarn("Il client %d ha eseguito un operazione su un file non aperto\n", fd_sk_client);


        sendInteger(fd_sk_client, SFILE_NOT_OPENED);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    } else if (!file_is_empty(f)) {
        pwarn("Il client %d ha eseguito una Write su un file non vuoto\n", fd_sk_client);


        sendInteger(fd_sk_client, SFILE_NOT_EMPTY);
        free(fcontent);
        str_clearArray(&split, n);
        return;
    }
    //da qui in poi il file viene inserito
    if (fsize > config.MAX_STORAGE) {  //se non ho spazio
        pwarn("Rilevata Capacity Miss\n");

        sendInteger(fd_sk_client, S_STORAGE_FULL);
        free_space(fd_sk_client, option, fsize);

        if (fsize > storage_left) {
            perr("Non è stato possibile liberare spazio\n");

            free(fcontent);
            str_clearArray(&split, n);
            sendInteger(fd_sk_client, S_FREE_ERROR);
            return;
        }

        printf("Spazio liberato!\n");
    }

    assert(f->path != NULL && !str_is_empty(f->path));
    assert(storage_left <= config.MAX_STORAGE);

    fprintf(stdout, "Storage left: %d\n\n", storage_left);
    fprintf(stdout, "File size: %d\n\n", fsize);

    file_update(&f, fcontent, fsize);
    storage_left -= fsize;

    fprintf(stdout, "Storage left: %d\n\n", storage_left);

    sendInteger(fd_sk_client, S_SUCCESS);

    fprintf(stdout, "SUCCESS INVIATA\n\n", storage_left);

    str_clearArray(&split, n);

    psucc("Write completata\n"
              "Capacità dello storage: %d\n\n", storage_left);
}

void createFile(int fd_sk_client, char *request) {
  size_t fsize = receiveInteger(fd_sk_client);

  char **split = NULL;
  int n = str_split(&split, request, ":");
  char *filepath = split[0];
  char *cpid = split[1];
  assert(!str_is_empty(filepath) && filepath != NULL);

  if (hash_containsKey(tbl_file_path, filepath)) {
    pwarn("Il client %d ha tentato di creare il file %s, che già esiste sul Server\n", fd_sk_client, (strrchr(filepath,'/')+1));
    perr("Richiesta non eseguita\n");
    sendInteger(fd_sk_client, SFILE_ALREADY_EXIST);

  } else if (fsize > config.MAX_STORAGE) { //se il file è troppo grande
    pwarn("Il client %d ha tentato di mettere un file troppo grande.\n", fd_sk_client);
    perr("Richiesta non eseguita.\n");
    sendInteger(fd_sk_client, SFILE_TOO_LARGE);

  } else if (storable_files_left == 0) {
    pwarn("Rilevata CAPACITY MISS\n", fd_sk_client);

    sendInteger(fd_sk_client, S_STORAGE_FULL);
    free_space(fd_sk_client, 'c', 0);
    pwarn("Impossibile espellere file\n", fd_sk_client);
    perr("Richiesta non eseguita\n");
    sendInteger(fd_sk_client, S_STORAGE_FULL);

  } else {
    assert(hash_containsKey(tbl_has_opened, cpid));

    fprintf(stdout, "filepath arrivato:\n____%s/\n", filepath);

    file_s *f = file_init(filepath);//creo un nuovo file

    fprintf(stdout, "file creato di dimensione %ld nome %s\n", f->size, f->path);

    if (f == NULL) {
      sendInteger(fd_sk_client, MALLOC_ERROR);
      return;
    }
    hash_insert(&tbl_file_path, filepath, f);	// lo memorizzo
    file_open(&f, cpid); 											// lo apro

    storable_files_left--;										// aggiorno il numero di file memorizzabili
    list_insert(&storage_fifo, filepath, f);  // e infine lo aggiungo alla coda fifo

    printf("FILE CREATO CON SUCCESSO\n\n");

    sendInteger(fd_sk_client, S_SUCCESS);			//notifico il client dell'esito positivo dell'operazione
  }
  str_clearArray(&split, n);
}

void closeFile(int fd_sk_client, char *request) {
    char **array = NULL;
    int n = str_split(&array, request, ":");
    assert(n == 2);


    char *filepath = array[0];
    char *cpid = array[1];

        fprintf(stdout, "filepath = %s\n", filepath);




    if (!hash_containsKey(tbl_file_path, filepath)) {

        pwarn("Il client %d ha eseguito un operazione su un file che non esiste\n", fd_sk_client);

        sendInteger(fd_sk_client, SFILE_NOT_FOUND);
        str_clearArray(&array, n);
        return;
    }


    file_s *f = hash_getValue(tbl_file_path, filepath);

    if (!file_is_opened_by(f, cpid)) {
            pwarn("Il client %d ha eseguito un operazione su un file non aperto\n", fd_sk_client);


        sendInteger(fd_sk_client, SFILE_NOT_OPENED);
        str_clearArray(&array, n);
        return;
    }

    client_closes_file(&f, cpid);
      fprintf(stdout, "CONNESSIONE CHIUSA\n");

    sendInteger(fd_sk_client, S_SUCCESS);
        psucc("File %s chiuso dal client %s\n\n", (strrchr(filepath,'/')+1), cpid);


    str_clearArray(&array, n);

}

// Funzione di rimozione dei file in caso di Capacity Misses.
void free_space(int fd_sk_client, char option, size_t fsize) {
    list_node *curr = storage_fifo->head;

    while (true) {
        if (curr == NULL) {   //ho finito di leggere la coda
                psucc("Lettura coda FIFO terminata\n\n");
            sendInteger(fd_sk_client, EOS_F);
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
                printf("Rimuovo il file %s dalla coda\n", (strrchr(f->path, '/')+1));

            if (option == 'y') {  //caso in cui il file viene espulso e inviato al client
                sendInteger(fd_sk_client, !EOS_F);
                sendStr(fd_sk_client, f->path);
                sendn(fd_sk_client, f->content, f->size);
            }

            if (option == 'c') {  //caso in cui il client tenta di creare un file,
                //ma il numero massimo di file memorizzabili è 0. Vengono quindi
                //inviati al Client i nomi dei file che stanno per essere espulsi

                sendInteger(fd_sk_client, !EOS_F);
                sendStr(fd_sk_client, f->path);
            }

            storage_left += f->size;
            if (storage_left > config.MAX_STORAGE) {
                //mi assicuro di rimanere nel range 0 <= x <= MAX_STORAGE

                storage_left = config.MAX_STORAGE;
            }

            hash_deleteKey(&tbl_file_path, f->path, &file_destroy);
            storable_files_left++;
            n_rimpiazzamenti_cache++;

            char* key=curr->key;
            curr=curr->next;
            list_remove(&storage_fifo,key,NULL);
        }
        else {
            curr=curr->next;
        }

        if (fsize <= storage_left && storable_files_left > 0) { //raggiunto lo spazio richiesto, esco
            sendInteger(fd_sk_client, EOS_F);
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
    free(file);
}

/* Funzione che simula l apertura del file f da parte del Client cpid.
 * */
void file_open(file_s **f, char *cpid) {
    list_insert(&(*f)->pidlist, cpid, NULL);

    fprintf(stdout, "file_open\n\n");

    hash_iterate(tbl_has_opened, printf, "%s %s %s\n\n");
        fprintf(stdout, "iterated\n\n");
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
        perr("Impossibile il file %s\n", (strrchr(path, '/') + 1));
        return NULL;
    }
    file->path = str_create(path);  //creo una copia del path per semplicità
    file->content = 0;
    file->size = 0;
    file->pidlist = list_create();
    return file;
}

void print_storage(){
  printf("STORAGE_FIFO:\n");
  int i=0;
  list_node *l = storage_fifo->head;
  while(l != NULL){
    printf(" %d: %s\n", i++, (char*)(l->key));
    l = l->next;
  }

  printf("\n");

  l = storage_fifo->head;
  file_s *prova = (file_s*)l->value;
  FILE* pf = (FILE*)prova->content;

  printf("CONTENT : %d\n\n", prova->content);

  printf("TBL_FILE_PATH:\n");
  hash_iterate(tbl_file_path, printf, "%s %d\n\n");

  printf("TBL_HAS_OPENED:\n");
  hash_iterate(tbl_has_opened, printf, "%s %d\n\n");



  //printf(" %s\n", (char*)(prova->content));

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
    storage_left -= (*f)->size;

    (*f)->content = newContent;
    (*f)->size = newSize;
}

/* Funzione che simula la chiusura del file f da parte del Client cpid.
 * */
void client_closes_file(file_s **f, char *cpid) {
    list_remove(&(*f)->pidlist, cpid, NULL);

    fprintf(stdout, "ORA ITERO tbl_has_opened\n");
    hash_iterate(tbl_has_opened, printf, "%s %s %s\n\n");


    int *n = (int *) hash_getValue(tbl_has_opened, cpid);
    *n -= 1;
    hash_updateValue(&tbl_has_opened, cpid, n, NULL);

    assert((*(int *) hash_getValue(tbl_has_opened, cpid)) >= 0);
}


void clear_openedFiles(char *key, void *value, bool *exit, void *cpid) {
    file_s *f = (file_s *) value;
    if (file_is_opened_by(f, (char *) cpid)) {
        client_closes_file(&f, (char *) cpid);
    }

    //per togliere il warning "parameter never used"
    key;
    exit;
}
