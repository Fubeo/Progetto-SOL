#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "./lib/customsocket.h"
#include "./lib/customstring.h"
#include "./lib/customprint.h"

#define SERVER_PATH "./tmp/serversock.sk"
#define BUFFER_LENGTH    250
#define FALSE              0

// Settings
char *sockfilename = NULL;        // -f
char *download_folder = NULL;     // -d
char *backup_folder = NULL;       // -D
bool print_all = false;           // -p
int sleep_between_requests = 0;   // -t


void print_commands();
int check_options(int argc, char *argv[]);
void execute_options(int argc, char *argv[]);


int main(int argc, char *argv[]){

  int sd = check_options(argc, argv);
  //execute_options(argc, argv);



  // client - server
  sendStr(sd, "Marcello");


  char *msg;
  msg = receiveStr(sd);
  fprintf(stdout, "Server: %s\n", msg);

  if (sd != -1)
  close(sd);
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

int check_options(int argc, char *argv[]){
  int sd = -1;
  bool found_rename = false;
  bool found_rR = false;
  bool found_wW = false;

  // controllo preliminare opzioni

  for (int i = 1; i < argc; i++) {
  fprintf(stdout,"%s con i=%d\n", argv[i], i);
    if (str_starts_with(argv[i], "-f")) {
      found_rename = true;
      sockfilename = ((argv[i]) += 2);

      fprintf(stdout, "%s\n", sockfilename);

      // connessione al server
      sd = client_unix_socket();
      client_unix_connect(sd, sockfilename);

    }

    else if (str_starts_with(argv[i], "-d")) download_folder = ((argv[i]) += 2);

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
      return -1;
  }

  if(!found_rR && download_folder != NULL){
      perr("L opzione -d va usata congiuntamente con l opzione -r o -R");
  }

  if(!found_wW && backup_folder != NULL){
      perr("L opzione -D va usata congiuntamente con l opzione -w o -W");
  }

  return sd;
}


void execute_options(int argc, char *argv[]){
  int opt, errcode;
  while ((opt = getopt(argc, argv, ":h:w:W:r:R::c:")) != -1) {
    switch (opt) {
      case 'h': {
        print_commands();
        break;
      }



      default: {
        printf("not supported\n");
        break;
      }
    }
  }
}
