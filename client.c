#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "./lib/customsocket.h"
#include "./lib/customstring.h"
#include "./lib/customprint.h"
#include "./lib/customerrno.h"
#include "./lib/customfile.h"

#define SERVER_PATH "./tmp/serversock.sk"
#define BUFFER_LENGTH    250

#define O_OPEN 0
#define O_CREATE 1
#define O_LOCK 2

static struct timespec timespec_new() {
    struct timespec timeToWait;
    struct timeval now;

    gettimeofday(&now, NULL);

    timeToWait.tv_sec = now.tv_sec;
    timeToWait.tv_nsec = (now.tv_usec + 1000UL * 1) * 1000UL;

    return timeToWait;
}

extern char *optarg;

// Settings
int sd = -1;
char *sockfilename = NULL;        // -f
char *download_folder = NULL;     // -d
char *backup_folder = NULL;       // -D
bool print_all = true;           	// -p
int sleep_between_requests = 0;   // -t

bool running = true;
bool connected = false;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;


void print_commands();
void check_options(int argc, char *argv[]);
void execute_options(int argc, char *argv[], int sd);

int open_connection(const char *sockname, int msec, const struct timespec abstime);
void exit_function();
int closeConnection(const char *sockname);
static void *thread_function(void *abs_t);

int close_file(const char *pathname);

// -w e -W
int open_file(char *pathname, int flags);
int write_file(const char *pathname, const char *dirname);
void send_file_to_server(const char *backup_folder, char *file);

// -R
int readNFiles(int N, const char *dirname);

// -r
int readFile(const char *pathname, void **buf, size_t *size);

// -c
int removeFile(const char *pathname);


int main(int argc, char *argv[]){
  int errcode;

  check_options(argc, argv);

  execute_options(argc, argv, sd);

  if (closeConnection(sockfilename) != 0) {
    errcode = errno;
    pcode(errcode, NULL);
  }

  if (sd != -1) close(sd);
}

void print_commands() {
    printf("\t-h \t\t\tPrints this helper.\n");
    printf("\t-f <sock> \t\tSets socket name to <sock>. \033[0;31m This option must be set once and only once. \033[0m\n");
    printf("\t-p \t\t\tIf set, every operation will be printed to stdout. \033[0;31m This option must be set at most once. \033[0m\n");
    printf("\t-t <time> \t\tSets the waiting time (in milliseconds) between requests. Default is 0.\n");
    printf("\t-a <time> \t\tSets the time (in seconds) after which the app will stop attempting to connect to server. Default value is 5 seconds. \033[0;31m This option must be set at most once. \033[0m\n");
    printf("\t-w <dir>[,<num>] \tSends the content of the directory <dir> (and its subdirectories) to the server. If <num> is specified, at most <num> files will be sent to the server.\n");
    printf("\t-W <file>{,<files>}\tSends the files passed as arguments to the server.\n");
    printf("\t-D <dir>\t\tWrites into directory <dir> all the files expelled by the server app. \033[0;31m This option must follow one of -w or -W. \033[0m\n");
    printf("\t-r <file>{,<files>}\tReads the files specified in the argument list from the server.\n");
    printf("\t-R[<num>] \t\tReads <num> files from the server. If <num> is not specified, reads all files from the server. \033[0;31m There must be no space bewteen -R and <num>.\033[0m\n");
    printf("\t-d <dir> \t\tWrites into directory <dir> the files read from server. If it not specified, files read from server will be lost. \033[0;31m This option must follow one of -r or -R. \033[0m\n");
    printf("\t-l <file>{,<files>} \tLocks all the files given in the file list.\n");
    printf("\t-u <file>{,<files>} \tUnlocks all the files given in the file list.\n");
    printf("\t-c <file>{,<files>} \tDeletes from server all the files given in the file list, if they exist.\n");
    printf("\n");
}

void check_options(int argc, char *argv[]){
  bool found_rename = false;
  bool found_rR = false;
  bool found_wW = false;

  // controllo preliminare opzioni

  for (int i = 1; i < argc; i++) {
    if (str_starts_with(argv[i], "-f")) {
      found_rename = true;
      sockfilename = ((argv[i]) += 2);
      if (open_connection(sockfilename, 0, timespec_new()) != 0) {
        pcode(errno, NULL);
        exit(errno);
      }

    } else if (str_starts_with(argv[i], "-d")) download_folder = ((argv[i]) += 2);

    else if (str_starts_with(argv[i], "-D")) backup_folder = ((argv[i]) += 2);

    else if (str_starts_with(argv[i], "-t")) str_toInteger(&sleep_between_requests, (argv[i]) += 2);

    else if (str_starts_with(argv[i], "-p")) print_all = true;

    else {
      if(str_starts_with(argv[i],"-r") || str_starts_with(argv[i],"-R")){
        found_rR = true;
      } else if(str_starts_with(argv[i],"-w") || str_starts_with(argv[i],"-W")){
        found_wW = true;
      }
      argv[argc] = argv[i];
      argv++;
    }
  }

  if (!found_rename) {
      perr( "Opzione Socket -f[sockfilename] non specificata\n");
      perr( "Inserire il Server a cui connettersi\n");
      return;
  }

  if(!found_rR && download_folder != NULL){
      perr("L opzione -d va usata congiuntamente con l opzione -r o -R");
  }

  if(!found_wW && backup_folder != NULL){
      perr("L opzione -D va usata congiuntamente con l opzione -w o -W");
  }
}

int open_connection(const char *sockname, int msec, const struct timespec abstime) {
    errno=0;
    sd = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un sa;
    strcpy(sa.sun_path, sockname);
    sa.sun_family = AF_UNIX;
    int status = connect(sd, (const struct sockaddr *) &sa, sizeof(sa));

    if(status==0){
        status = (int)receiveInteger(sd);
        if(status==CONNECTION_REFUSED){
            errno=CONNECTION_REFUSED;
            return -1;
        }
        sockfilename = str_create(sockname); //faccio una copia per togliere il warning discard qualifier
        char* mypid= str_long_toStr(getpid());
        sendn(sd,mypid, str_length(mypid));

        atexit(exit_function);
        connected=true;
        free(mypid);
        return 0;
    }

    pthread_t tid;
    pthread_create(&tid, NULL, &thread_function, (void *) &abstime);

    while (running) {
        status = connect(sd, (const struct sockaddr *) &sa, sizeof(sa));
        if(status==0){
            status = (int)receiveInteger(sd);
            if(status==CONNECTION_REFUSED){
                errno=CONNECTION_REFUSED;
                pthread_join(tid,NULL);
                return -1;
            }

            sockfilename = str_create(sockname);
            char* mypid= str_long_toStr(getpid());
            sendn(sd,mypid, str_length(mypid));
            atexit(exit_function);
            connected=true;
            return 0;
        }
        usleep(msec * 1000);
    }

    pthread_join(tid,NULL);
    errno=CONNECTION_TIMED_OUT;
    return -1;
}

static void *thread_function(void *abs_t) {
    struct timespec *abs = (struct timespec *) abs_t;

    pthread_mutex_lock(&lock);
    pthread_cond_timedwait(&cond, &lock, abs);

    running = false;
    pthread_mutex_unlock(&lock);
    return NULL;
}

void exit_function(){
    if(connected)
        closeConnection(sockfilename);
}

int closeConnection(const char *sockname) {
    errno=0;

    if(sockname==NULL){
        return 0;
    }

    if (!str_equals(sockname, sockfilename)) {
        errno = WRONG_SOCKET;
        return -1;
    }
    char* client_pid = str_long_toStr(getpid());
    char* request = str_concat("e:",client_pid);

    sendn(sd, request, strlen(request));
    free(client_pid);
    free(request);

    int status = (int)receiveInteger(sd);

    if(status != S_SUCCESS){
        if(status==SFILES_FOUND_ON_EXIT){
            pcode(status, NULL);
        }
    }

    if(close(sd) != 0){
        perr("Errore nella chiusura del Socket\n"
             "Codice errore: %s\n\n", strerror(errno));
        return -1;
    }
    connected=false;
    free(sockfilename);
    //per rimuovere i warning
    pcolor(STANDARD, "");

    return 0;
}

void execute_options(int argc, char *argv[], int sd){
  int opt, errcode;
  while ((opt = getopt(argc, argv, ":h:w:W:r:R:c:")) != -1) {

    switch (opt) {
      case 'h': {
        print_commands();
        break;
      }

      case 'W': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          files[i] = realpath(files[i],NULL);
          send_file_to_server(backup_folder, files[i]);
          usleep(sleep_between_requests * 1000);
        }
        str_clearArray(&files, n);
        break;
      }

      case 'w': {
        char **array = NULL;
        int n_files = -1;

        int count;
        int n = str_split(&array, optarg, ",");

        if (n > 2) {
          perr("Troppi argomenti per il comando -w dirname[,n=x]\n");
          break;
        }

        if (n == 2)
          if(str_toInteger(&n_files, array[1]) != 0){
            perr("%s non è un numero\n", optarg);
            break;
          }

        // lettura dei path dei files nella directory
        char **files = NULL;
        count = file_nscanAllDir(&files, array[0], n_files);

        for (int i = 0; i < count; i++) {
          send_file_to_server(backup_folder, files[i]);
          usleep(sleep_between_requests * 1000);
        }

        str_clearArray(&array, n);
        //str_clearArray(&files, count);
        break;
      }

      case 'R': {
          int n = 0;
          if (optarg != NULL) {
              //optarg++;
              if (str_toInteger(&n, optarg) != 0) {
                  perr("%s non è un numero\n", optarg);
                  break;
              }
          }
          if (readNFiles(n, download_folder) != 0) {
              errcode = errno;
              pcode(errcode, NULL);
          } else if(print_all){
              psucc("Ricevuti %d file\n\n", n);
          }
          break;
      }

      case 'r': {
          char **files = NULL;
          int n = str_split(&files, optarg, ",");
          void *buff;
          size_t size;
          for (int i = 0; i < n; i++) {
              fprintf(stdout, "FILE: %s\n", files[i]);
              if(open_file(files[i], O_OPEN) == 0) {
                  fprintf(stdout, "FILE APERTO\n");           // apro il file
                  if (readFile(files[i], &buff, &size) != 0) {    // lo leggo
                      perr("ReadFile: Errore nel file %s\n", files[i]);
                      errcode = errno;
                      pcode(errcode, files[i]);
                  } else {    // a questo punto o salvo i file nella cartella dirname (se diversa da NULL) o invio un messaggio di conferma
                      char *filename = strrchr(files[i], '/') + 1;
                      if (download_folder != NULL) {
                          if (!str_ends_with(download_folder, "/")) {
                              download_folder = str_concat(download_folder, "/");
                          }
                          char *path = str_concatn(download_folder, filename, NULL);
                          FILE *file = fopen(path, "wb");
                          if (file == NULL) {
                              perr("Cartella %s sbagliata\n", path);
                              download_folder = NULL;
                          } else {
                              if (fwrite(buff, sizeof(char), size, file) == 0) {  // scrivo il file ricevuto nella cartella download_folder
                                  perr("Errore nella scrittura di %s\n"
                                       "I successivi file verranno ignorati\n", path);
                                  download_folder = NULL;
                              } else if (print_all) {
                                  pcolor(GREEN, "File \"%s\" scritto nella cartella: ", filename);
                                  printf("%s\n\n", path);
                              }
                              fclose(file);
                          }
                          free(path);
                      } else if (print_all) {
                          psucc("Ricevuto file: ");
                          printf("%s\n\n", filename);
                      }
                      free(buff);
                }
                if(close_file(files[i])!=0){
                    perr("close_file: Errore nella chiusura del file %s\n", files[i]);
                    errcode = errno;
                    pcode(errcode, files[i]);
                }
            }else{
                perr("open_file: Errore nell apertura del file %s\n", files[i]);
                errcode = errno;
                pcode(errcode, files[i]);
              }
            usleep(sleep_between_requests * 1000);
          }

        str_clearArray(&files, n);
        break;
      }

      case 'c': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          if (removeFile(files[i]) != 0) {
            errcode = errno;
            pcode(errcode, files[i]);
            perr( "RemoveFile: error occurred on file %s\n", files[i]);
          } else if(print_all){
            psucc("File %s successfully removed\n\n", files[i]);
          }
          usleep(sleep_between_requests * 1000);
        }
        str_clearArray(&files, n);
        break;
      }

      default: {
        printf("The requested operation is not supported\n");
        break;
      }
    }
  }

}

void send_file_to_server(const char *backup_folder, char *file) {
    int errcode;
    if(print_all) printf("Sending \n%s\n", file);
    if (open_file(file, O_CREATE) != 0) {
        pcode(errno, file);
        return;
    }

    if (write_file(file, backup_folder) != 0) {
        fprintf(stderr, "Writefile: error sending file %s\n", file);
        errcode = errno;
        pcode(errcode, file);
    } else if(print_all){
        psucc("File \"%s\" successfully sent\n\n", strrchr(file, '/') + 1);
    }

    close_file(file);
}

int open_file(char *pathname, int flags) {
    errno=0;

    if(pathname==NULL){
        return 0;
    }

    int response;
    char* client_pid=str_long_toStr(getpid());

    switch (flags) {
        case O_OPEN : {
            char *cmd = str_concatn("o:",pathname,":", client_pid, NULL);

            // invio il comando al server
            sendn(sd, cmd, str_length(cmd));

            // attendo una sua risposta
            response = (int)receiveInteger(sd);
            fprintf(stdout, "FILE APERTO\n");

            if (response != 0) {
                errno=response;
                return -1;
            }
            free(cmd);
            break;
        }
        case O_CREATE : {
          fprintf(stdout, "pathname = %s\n", pathname);

          if(pathname == NULL){
              errno = FILE_NOT_FOUND;
              perr("FILE NOT FOUND");
              return -1;
          }
          FILE* file = fopen(pathname,"rb");
          size_t fsize = file_getsize(file);

          char *cmd = str_concatn("c:", pathname, ":", client_pid, NULL);

          //invio il comando al server
          sendn(sd, (char*)cmd, str_length(cmd));  //voglio creare un file
          sendInteger(sd, fsize);                  //che inizialmente ha grandezza fsize

          //attendo una sua risposta
          response = (int)receiveInteger(sd);

          if(response == S_STORAGE_FULL){
              while ((int) receiveInteger(sd)!=EOS_F){
                  char* s= receiveStr(sd);
                  pwarn("WARNING: Il file %s è stato espulso\n"
                        "Aumentare la capacità del Server per poter memorizzare più file!\n\n", (strrchr(s,'/')+1));
                  free(s);
              }
              response = (int)receiveInteger(sd);
          }
          if(response == SFILE_ALREADY_EXIST){
            errno = SFILE_ALREADY_EXIST;
            pwarn("WARNING: Il file %s e' gia' presente sul server!\n\n");
            return -1;
          }

          if (response != S_SUCCESS) {
              free(cmd);
              free(pathname);
              free(client_pid);
              errno=response;

              return -1;
          }
          free(cmd);

          break;
        }

        case O_LOCK : {
            printf("not implemented yet...\n");
            response = -1;
            break;
        }

        default: {
            fprintf(stderr, "flags argument error, use O_CREATE or O_LOCK\n");
            response = -1;
            break;
        }
    }
    free(client_pid);
    return response;
}

int close_file(const char *pathname) {
  errno = 0;

  if(pathname==NULL){
    return 0;
  }

  char *client_pid=str_long_toStr(getpid());
  char *request = str_concatn("cl:", pathname,":",client_pid,NULL);
  sendn(sd, request, str_length(request));

  free(request);
  free(client_pid);


  int status = (int)receiveInteger(sd);
  if(status != 0){
    errno=status;
    return -1;
  }
  fprintf(stdout, "HO CHIUSO %s\n", pathname);
  return 0;
}

int write_file(const char *pathname, const char *dirname) {

    if(pathname==NULL && dirname != NULL){
        errno=INVALID_ARG;
        return -1;
    }

    if(pathname==NULL){
        return 0;
    }

	   char* client_pid=str_long_toStr(getpid());

      errno=0;

    //costruisco la key

    if(pathname==NULL) {
        errno=FILE_NOT_FOUND;
        return -1;
    }

    //mando la richiesta di scrittura al Server -> key: abs_path | value: file_content
    char *request;
    if (dirname != NULL) {
        request = str_concatn("w:", pathname, ":", client_pid,"?", "y", NULL);
    } else {
        request = str_concatn("w:", pathname, ":", client_pid,"?", "n", NULL);
    }


    sendn(sd, request, str_length(request));

    fprintf(stdout, "\n\nINVIATI: \n REQ -> %s\n LEN -> %d\n\n\n", request, str_length(request));

    if(sendfile(sd, pathname)==-1){
        perr("Malloc error\n");
        return -1;
    }

    //free(pathname);
    //free(request);
    //free(client_pid);

    int status = (int)receiveInteger(sd);

    fprintf(stdout, "STATUS: %d\n\n", status);

    if(status == S_SUCCESS){
        return 0;
    }

    if(status != S_STORAGE_FULL){
        errno=status;
        return -1;
    }

    if(dirname != NULL){
        char* dir=NULL;

        if (!str_ends_with(dirname, "/")) {
            dir = str_concat(dirname, "/");
        } else {
            dir = str_create(dirname);
        }

        pwarn("CAPACITY MISS: Ricezione file espulsi dal Server...\n\n");
        while(((int)receiveInteger(sd))!=EOS_F){
            char* filepath=receiveStr(sd);

            char* filename=strrchr(filepath,'/')+1;
            char *path = str_concat(dir, filename);
            pwarn("Scrittura del file \"%s\" nella cartella \"%s\" in corso...\n", filename, dir);

            void* buff;
            size_t n;
            receivefile(sd,&buff,&n);
            FILE* file=fopen(path,"wb");
            if(file==NULL){
                perr("Impossibile creare un nuovo file, libera spazio!\n");
            }else {
                fwrite(buff, sizeof(char), n, file);
                psucc("Download completato!\n\n");
                fclose(file);
            }
            free(buff);
            free(filepath);
            free(path);
        }

        free(dir);
    }else{
        receiveInteger(sd); //attendo l EOS
    }

    status = (int)receiveInteger(sd);
    if(status != S_SUCCESS) {
        errno = status;
        return -1;
    }

        request = "s";
            sendn(sd, request, str_length(request));

    return 0;
}


int readNFiles(int N, const char *dirname) {
    errno=0;
    char* dir=NULL;

    if(dirname != NULL) {
        if (!str_ends_with(dirname, "/")) {
            dir = str_concat(dirname, "/");
        } else{
            dir = str_create(dirname);
        }
    }

    // Invio la richiesta al Server
    char* n = str_long_toStr(N);
    char *request = str_concat("rn:", n);
    sendStr(sd, request);

    //se la risposta è ok, lo storage non è vuoto
    int response = (int)receiveInteger(sd);
    if(response != 0) {
        errno = response;
        return -1;
    }

    size_t size;
    void* buff;
    if (dir != NULL) {
        //ricevo i file espulsi
        while((int) receiveInteger(sd)!=EOS_F) {
            char *filepath = receiveStr(sd);

            char* file_name=strrchr(filepath,'/')+1;
            receivefile(sd,&buff,&size);

            char *path = str_concat(dir, file_name);
            FILE *file = fopen(path, "wb");
            if (file == NULL) { //se dirname è invalido, viene visto subito
                errno=INVALID_ARG;
                return -1;
            }
            fwrite(buff, sizeof(char), size, file);
            fclose(file);

            free(buff);
            free(path);
            free(filepath);
        }
    } else{
        while((int) receiveInteger(sd) != EOS_F) {
            char* filepath=receiveStr(sd);
            free(filepath);
            receivefile(sd,&buff,&size);
            free(buff);
        }
    }
    free(request);
    free(dir);
    free(n);
    return 0;
}

int readFile(const char *pathname, void **buf, size_t *size) {
    errno=0;

    if(pathname==NULL){
        return 0;
    }

    char* client_pid=str_long_toStr(getpid());


    //mando la richiesta al server
    char *request = str_concatn("r:", pathname, ":", client_pid, NULL);
    sendn(sd, request, str_length(request));

    //attendo una risposta
    int response = (int)receiveInteger(sd);
    if (response == 0) {    //se il file esiste
        receivefile(sd,buf,size);
        free(request);
        free(client_pid);
        return 0;
    }
    free(client_pid);
    free(request);
    errno=response;
    return -1;
}

int removeFile(const char *pathname) {
    errno=0;

    if(pathname==NULL)
        return 0;

    char *request = str_concat("rm:", pathname);
    sendn(sd, request, str_length(request));
    int status = (int)receiveInteger(sd);

    if(status != 0){
        errno=status;
        return -1;
    }
    free(request);
    return 0;
}
