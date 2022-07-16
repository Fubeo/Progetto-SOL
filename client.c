#define _GNU_SOURCE
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
#include <assert.h>
#include "./lib/customsocket.h"
#include "./lib/customstring.h"
#include "./lib/customprint.h"
#include "./lib/customerrno.h"
#include "./lib/customfile.h"
#include "./lib/clientapi.h"

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
extern bool print_all;

// Settings
int sleep_between_requests = 0;   // -t
bool requested_o_lock = false;    // -L
char *download_folder = NULL;     //-d
char *backup_folder = NULL;       //-D


char *check_options(int argc, char *argv[]);
void execute_options(int argc, char *argv[]);
void print_commands();

int main(int argc, char *argv[]){
  int errcode;

  char * sockfilename = check_options(argc, argv);
  if(sockfilename == NULL) return -1;

  execute_options(argc, argv);

  if (closeConnection(sockfilename) != 0) {
    errcode = errno;
    pcode(errcode, NULL);
  }
}

void print_commands() {
    printf("\t-h \t\t\tPrints this helper.\n");
    printf("\t-f <sock> \t\tSets socket name to <sock>. \033[0;31m This option must be set once and only once. \033[0m\n");
    printf("\t-w <dir>[,<num>] \tSends the content of the directory <dir> (and its subdirectories) to the server. If <num> is specified, at most <num> files will be sent to the server.\n");
    printf("\t-W <file>{,<files>}\tSends the files passed as arguments to the server.\n");
    printf("\t-L \tWorks only if used with -w or -W. Sets lock flag on all specified files.\n");
    printf("\t-D <dir>\t\tWrites into directory <dir> all the files expelled by the server. \033[0;31m This option must follow one of -w or -W. \033[0m\n");
    printf("\t-R[<num>] \t\tReads <num> files from the server. If <num> is not specified, reads all files from the server. \033[0;31m There must be no space bewteen -R and <num>.\033[0m\n");
    printf("\t-r <file>{,<files>}\tReads the files specified in the argument list from the server.\n");
    printf("\t-a <file>,<file> \tSends the second argument to the server, which will concat it with the first argument file.\n");
    printf("\t-o <file>{,<files>}\tOpens the files specified in the argument list from the server.\n");
    printf("\t-d <dir> \t\tWrites into directory <dir> the files read from server. If it not specified, files read from server will be lost. \033[0;31m This option must follow one of -r or -R. \033[0m\n");
    printf("\t-t <time> \t\tSets the waiting time (in milliseconds) between requests. Default is 0.\n");
    printf("\t-p \t\t\tIf set, every operation will be printed to stdout. \033[0;31m This option must be set at most once. \033[0m\n");
    printf("\t-l <file>{,<files>} \tLocks all the files given in the file list.\n");
    printf("\t-u <file>{,<files>} \tUnlocks all the files given in the file list.\n");
    printf("\t-c <file>{,<files>} \tDeletes from server all the files given in the file list, if they exist.\n");
    printf("\n");
}

char *check_options(int argc, char *argv[]){
  bool found_rename = false;
  bool found_rR = false;
  bool found_wW = false;
  char *sockfilename = NULL;

  for (int i = 1; i < argc; i++) {
    if (str_starts_with(argv[i], "-f")) {
      found_rename = true;
      sockfilename = ((argv[i]) += 2);
      if (openConnection(sockfilename, 0, timespec_new()) != 0) {
        pcode(errno, NULL);
        exit(errno);
      }

    }
    else if (str_starts_with(argv[i], "-d")) download_folder = ((argv[i]) += 2);
    else if (str_starts_with(argv[i], "-D")) backup_folder = ((argv[i]) += 2);
    else if (str_starts_with(argv[i], "-t")) str_toInteger(&sleep_between_requests, (argv[i]) += 2);
    else if (str_starts_with(argv[i], "-p")) {print_all = true; (argv[i]) += 2;}
    else if (str_starts_with(argv[i], "-L")) {requested_o_lock = true; (argv[i]) += 2;}

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
      return NULL;
  }

  if(!found_rR && download_folder != NULL)
      perr("Option -d must be used with -r o -R");

  if(!found_wW && backup_folder != NULL)
      perr("Option -D must be used with -w o -W");

  return sockfilename;
}

void execute_options(int argc, char *argv[]){
  int opt, errcode;
  while ((opt = getopt(argc, argv, ":h:W:w:R::r:c:l:u:a:o:C:")) != -1) {
    fprintf(stdout, "%d: NUOVA OPT %c\n", getpid(), opt);

    switch (opt) {
      case 'h': {
        print_commands();
        break;
      }

      case 'W': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          char *s = realpath(files[i], NULL);
          if(errno == ENOENT){
            perr( "WriteFile: file %s does not exist\n", files[i]);
          } else {
            send_file_to_server(backup_folder, files[i]);
            usleep(sleep_between_requests * 1000);
          }
          free(s);
        }
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

        if((n == 2) && str_toInteger(&n_files, array[1]) != 0){
          perr("%s is not a number\n", optarg);
          break;
        }

        // lettura dei path dei files nella directory
        char **files = NULL;
        count = file_nscan(&files, array[0], n_files);

        for (int i = 0; i < count; i++) {
          send_file_to_server(backup_folder, files[i]);
          usleep(sleep_between_requests * 1000);
        }

        break;
      }

      case 'o': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          if (openFile(files[i], O_OPEN) != 0) {
            errcode = errno;
            pcode(errcode, files[i]);
            perr( "OpenFile: error occurred on file %s\n", files[i]);
          } else if(print_all){
            if(print_all) psucc("File %s successfully opened\n", files[i]);
          }
          usleep(sleep_between_requests * 1000);
        }
        str_clearArray(&files, n);
        break;
      }

      case 'R': {
          int n = 0;
          if (optarg != NULL) {
              if (str_toInteger(&n, optarg) != 0) {
                  perr("%s is not a number\n", optarg);
                  break;
              }
          }
          if (readNFiles(n, download_folder) != 0) {
              errcode = errno;
              pcode(errcode, NULL);
          } else if(print_all){
              if(n == 0)
              psucc("Files successfully read\n");
          }
          fprintf(stdout, "%d: FINITO READN\n", getpid());

          break;
      }

      case 'r': {
          char **files = NULL;
          int n = str_split(&files, optarg, ",");
          void *buff;
          size_t size;
          for (int i = 0; i < n; i++) {
              if(openFile(files[i], O_OPEN) == 0) { // Apertura del file
                  if (readFile(files[i], &buff, &size) != 0) {    // Lettura del file
                      perr("ReadFile: Error on file %s\n", files[i]);
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
            }else{
                perr("openFile: Errore nell apertura del file %s\n", files[i]);
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
            psucc("File %s successfully removed\n", files[i]);
          }
          usleep(sleep_between_requests * 1000);
        }
        str_clearArray(&files, n);
        break;
      }

      case 'a': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        FILE* file = fopen(files[1],"rb");
        if(file == NULL){
            return;
        }
        size_t file_size = file_getsize(file);

        // Lettura del file
        void* file_content = file_read_all(file);
        if(file_content == NULL){
            fclose(file);
            str_clearArray(&files, n);
            return;
        }
        fclose(file);

        if (appendToFile(files[0], file_content, file_size, backup_folder)!= 0) {
          errcode = errno;
          pcode(errcode, files[1]);
          perr( "AppendFile: error occurred while appending %s to %s\n", (strrchr(files[1],'/')+1), (strrchr(files[0],'/')+1));
        } else if(print_all){
          psucc("File %s successfully appended\n", optarg);
        }
        free(file_content);
        str_clearArray(&files, n);
        break;
      }

      case 'l': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          char* rp = realpath(files[i], NULL);
          if(errno == ENOENT){
            perr( "LockFile: file %s does not exist\n", files[i]);
          } else {
            lockFile(files[i]);
            usleep(sleep_between_requests * 1000);
            free(rp);
          }
        }
        str_clearArray(&files, n);
        break;
      }

      case 'u': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          files[i] = realpath(files[i], NULL);
          if(errno == ENOENT){
            perr( "UnlockFile: file %s does not exist\n", optarg);
          } else {
            unlockFile(files[i]);
            usleep(sleep_between_requests * 1000);
          }
        }
        str_clearArray(&files, n);
        break;
      }

      case 'C': {
        char **files = NULL;
        int n = str_split(&files, optarg, ",");
        for (int i = 0; i < n; i++) {
          files[i] = realpath(files[i], NULL);
          if(errno == ENOENT){
            perr( "CloseFile: file %s does not exist\n", files[i]);
          } else {
            closeFile(files[i]);
            usleep(sleep_between_requests * 1000);
          }
        }
        break;
      }

      default: {
        printf("The requested operation is not supported\n");
        break;
      }
    }
  }
    fprintf(stdout, "%d: FINITO READN\n", getpid());

}

void send_file_to_server(const char *backup_folder, char *file) {
    int errcode;

    char *f = malloc(strlen(file)*sizeof(char)+1);
    strcpy(f, file);

    int o = ((requested_o_lock) ? O_LOCK : O_CREATE);

    extern bool o_lock_exists;
    if (openFile(f, o) != 0) {
        if(o == O_LOCK)
          if(!o_lock_exists) fprintf(stderr, "OpenLock: error creating file %s\n", file);
          else fprintf(stderr, "OpenLock: error opening file %s\n", file);
        else fprintf(stderr, "CreateFile: error creating file %s\n", file);
        errcode = errno;
        pcode(errcode, file);
        free(f);
        return;
    }
    if((o == O_LOCK && !o_lock_exists) || o == O_CREATE){
      if (writeFile(f, backup_folder) != 0) {
          if(print_all) pcolor(CYAN, "Sending file %s%s\n", "\x1B[0m", file);
          fprintf(stderr, "Writefile: error sending file %s\n", file);
          errcode = errno;
          pcode(errcode, f);
      } else if(print_all){
          psucc("File \"%s\" successfully sent\n", strrchr(file, '/') + 1);
      }
    }
}
