#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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
#include "../clientapi.h"
#include "../customstring.h"
#include "../customfile.h"
#include "../customerrno.h"
#include "../customsocket.h"

int sd = -1;

char *sockfilename = NULL;        // -f

bool running = true;
bool connected = false;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

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

int openConnection(const char *sockname, int msec, const struct timespec abstime) {
    errno = 0;
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
    char* request = str_concat("e:", client_pid);

    sendn(sd, request, strlen(request));
    free(client_pid);
    free(request);

    int status = (int)receiveInteger(sd);

    if(status != S_SUCCESS){
      if(status == SFILES_FOUND_ON_EXIT){
        pcolor(CYAN, "Server is closing files still opened\n");
      }
    }

    if(close(sd) != 0){
        perr("Error closing socket\nErrcode: %s\n\n", strerror(errno));
        return -1;
    }
    connected = false;
    free(sockfilename);
    //per rimuovere i warning
    pcolor(STANDARD, "");

    return 0;
}

int openFile(char *pathname, int flags) {
    errno = 0;

    if(pathname == NULL){ return 0; }

    int response;
    char* client_pid=str_long_toStr(getpid());

    switch (flags) {
        case O_OPEN : {
          char *cmd = str_concatn("o:",pathname,":", client_pid, NULL);

          // invio il comando al server
          sendn(sd, cmd, str_length(cmd));

          // attendo una sua risposta
          response = (int)receiveInteger(sd);

          free(cmd);

          if(response == SFILE_ALREADY_OPENED) response = S_SUCCESS;

          if (response != 0) {
            errno=response;
            return -1;
          }
          break;
        }

        case O_CREATE : {
          // Apro il file
          if(pathname == NULL){
            errno = FILE_NOT_FOUND;
            perr("FILE NOT FOUND");
            return -1;
          }
          FILE* file = fopen(pathname, "rb");
          size_t fsize = file_getsize(file);
          char *cmd = str_concatn("c:", pathname, ":", client_pid, NULL);

          // Invio del comando al server
          sendn(sd, (char*)cmd, str_length(cmd));
          sendInteger(sd, fsize);

          fclose(file);

          // Attesa della risposta del server
          response = (int)receiveInteger(sd);

          if(response == S_STORAGE_FULL){
            while ((int) receiveInteger(sd)!=EOS_F){
              char* s = receiveStr(sd);
              pwarn("WARNING: file %s has been removed\n"
                    "Increase storage space to store more files!\n\n", (strrchr(s,'/')+1));
              free(s);
            }
          }

          if(response == SFILE_ALREADY_EXIST){
            errno = SFILE_ALREADY_EXIST;
            pwarn("WARNING: file %s is already stored on the server!\n\n");
            return -1;
          }

          if (response != S_SUCCESS) {
            free(cmd);
            free(pathname);
            free(client_pid);
            errno = response;

            return -1;
          }

          free(cmd);
          break;
        }

        case O_LOCK : {
          char *cmd = str_concatn("co:", pathname, ":", client_pid, NULL);
          // Invio del comando al server
          sendn(sd, (char*)cmd, str_length(cmd));

          response = (int)receiveInteger(sd);
          if(response == SFILE_ALREADY_EXIST){    // Apri il file in modalita' locked
            response = (int)receiveInteger(sd);
            if(response == S_SUCCESS){
              errno=response;
              return -1;
            }
          } else if(response == SFILE_NOT_FOUND){ // Crea il file in modalita' locked

            if(pathname == NULL){
              errno = FILE_NOT_FOUND;
              sendInteger(sd, SFILE_NOT_FOUND);
              perr("FILE NOT FOUND");
              return -1;
            }
            sendInteger(sd, S_SUCCESS);

            FILE* file = fopen(pathname, "rb");
            size_t fsize = file_getsize(file);

            sendInteger(sd, fsize);

            response = (int)receiveInteger(sd);

            if (response != S_SUCCESS) {
              free(cmd);
              free(pathname);
              free(client_pid);
              errno = response;
              return -1;
            }

            free(cmd);
            break;
          }
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

int closeFile(const char *pathname) {
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
  return 0;
}

int writeFile(const char *pathname, const char *dirname) {

    if(pathname==NULL && dirname != NULL){
        errno=INVALID_ARG;
        return -1;
    }

    if(pathname==NULL){
        return 0;
    }

	   char* client_pid=str_long_toStr(getpid());

      errno=0;

    // costruisco la key

    if(pathname==NULL) {
        errno=FILE_NOT_FOUND;
        return -1;
    }

    // mando la richiesta di scrittura al Server -> key: abs_path | value: file_content
    char *request;
    if (dirname != NULL) {
        request = str_concatn(pathname, ":", client_pid,"?", "y", NULL);
    } else {
        request = str_concatn(pathname, ":", client_pid,"?", "n", NULL);
    }

    sendn(sd, request, str_length(request));

    if(sendfile(sd, pathname)==-1){
        perr("Malloc error\n");
        return -1;
    }


    free(request);
    free(client_pid);

    int status = (int)receiveInteger(sd);

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

        pwarn("CAPACITY MISS: Receiving ejected data...\n\n");
        while(((int)receiveInteger(sd))!=EOS_F){
          char* filepath=receiveStr(sd);

          char* filename=strrchr(filepath,'/')+1;
          char *path = str_concat(dir, filename);
          pwarn("Writing file \"%s\" into folder \"%s\"...\n", filename, dir);

          void* buff;
          size_t n;
          receivefile(sd,&buff,&n);
          FILE* file=fopen(path,"wb");
          if(file==NULL){
              perr("Unable to create a new file, not enough storage\n");
          } else {
              fwrite(buff, sizeof(char), n, file);
              psucc("Download completed\n\n");
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

    return 0;
}

int readNFiles(int N, const char *dirname) {
    errno = 0;
    char* dir = NULL;

    if(dirname != NULL) {
        if (!str_ends_with(dirname, "/")) {
            dir = str_concat(dirname, "/");
        } else{
            dir = str_create(dirname);
        }
    }

    // Invio la richiesta al Server
    char *client_pid = str_long_toStr(getpid());
    char *n = str_long_toStr(N);
    char *request = str_concatn("rn:", n, ":", client_pid, NULL);
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
        while((int) receiveInteger(sd) != EOS_F) {
            char *filepath = receiveStr(sd);

            char* file_name=strrchr(filepath,'/')+1;
            receivefile(sd,&buff,&size);

            char *path = str_concat(dir, file_name);
            FILE *file = fopen(path, "wb");
            if (file == NULL) { //se dirname è invalido, viene visto subito
                errno = INVALID_ARG;
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
            char* filepath = receiveStr(sd);
            free(filepath);
            receivefile(sd, &buff, &size);
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


    //mando la richiesta al server
    char *client_pid = str_long_toStr(getpid());
    char *request = str_concatn("r:", pathname, ":", client_pid, NULL);
    sendn(sd, request, str_length(request));

    //attendo una risposta
    int response = (int)receiveInteger(sd);



    if (response == S_SUCCESS) {    //se il file esiste
        receivefile(sd,buf,size);
        free(request);
        free(client_pid);
        return 0;
    }
    if(response == SFILE_NOT_FOUND) perr("ERROR: Requested file is not stored in the server");
    if(response == SFILE_NOT_OPENED) perr("ERROR: Requested file is not opened by this client");
    if(response == SFILE_LOCKED) perr("ERROR: Requested file is currently locked by another client");

    free(client_pid);
    free(request);
    errno=response;
    return -1;
}

int removeFile(const char *pathname) {
    errno=0;

    if(pathname==NULL)
        return 0;

    char* client_pid=str_long_toStr(getpid());
    char *request = str_concatn("rm:", pathname, ":", client_pid, NULL);
    sendn(sd, request, str_length(request));
    int status = (int)receiveInteger(sd);

    if(status != 0){
        errno=status;
        return -1;
    }
    free(request);
    return 0;
}

int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname) {
    errno=0;

    char* client_pid=str_long_toStr(getpid());
    char *request;
    if (dirname != NULL)
        request = str_concatn("a:", pathname,":", client_pid, "?y", NULL);
    else
        request = str_concatn("a:", pathname,":", client_pid, "?n" ,NULL);

    //invio la richiesta
    sendStr(sd, request);
    sendn(sd, buf, size);//invio il contenuto da appendere

    free(client_pid);
    free(request);

    int status = (int)receiveInteger(sd);

    if(status==S_SUCCESS){
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
                perr("Impossibile creare il file %s, libera spazio sul disco!\n", filename);
            } else {
                fwrite(buff, sizeof(char), n, file);
                fclose(file);
            }

            free(buff);
            free(path);
            free(filepath);
            psucc("Download completed!\n\n");
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

    return 0;
}

int lockFile(const char*pathname){
  char *client_pid = str_long_toStr(getpid());
  char *cmd = str_concatn("l:", pathname, ":", client_pid, NULL);
  sendn(sd, (char*)cmd, str_length(cmd));
  free(cmd);

    // Attesa della risposta del server
    int response = (int)receiveInteger(sd);

    if(response == SFILE_NOT_FOUND){
      errno = SFILE_NOT_FOUND;
      pwarn("WARNING: file %s does not exist on the server!\n\n", pathname);
      return -1;
    }

    if(response == SFILE_WAS_REMOVED){
      errno = SFILE_WAS_REMOVED;
      pwarn("WARNING: file %s has been removed before server could access to it!\n\n", pathname);
      return -1;
    }

    if(response == S_SUCCESS) {
      psucc("File %s successfully locked\n", pathname);
      return 0;
    }

    return -1;
}

int unlockFile(const char*pathname){
  char *client_pid = str_long_toStr(getpid());
  char *cmd = str_concatn("u:", pathname, ":", client_pid, NULL);
  sendn(sd, (char*)cmd, str_length(cmd));
  free(cmd);

  // Attesa della risposta del server
  int response = (int)receiveInteger(sd);

  if(response == SFILE_NOT_FOUND){
    errno = SFILE_NOT_FOUND;
    pwarn("WARNING: file %s does not exist on the server!\n\n", pathname);
    return -1;
  }

  if(response == SFILE_NOT_LOCKED){
    errno = SFILE_NOT_LOCKED;
    pwarn("WARNING: file %s is not locked!\n\n", pathname);
    return -1;
  }

  if(response == CLIENT_NOT_ALLOWED){
    errno = CLIENT_NOT_ALLOWED;
    pwarn("WARNING: file %s is locked by another client!\n\n", pathname);
    return -1;
  }

  if(response == S_SUCCESS) {
    psucc("File %s successfully unlocked\n", pathname);
    return 0;
  }

  return -1;
}
