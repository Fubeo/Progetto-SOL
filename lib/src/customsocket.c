#include "../customsocket.h"
//#include "../file_reader.h"

int server_unix_socket(char *sockpath) {
    int fd_sk_server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_sk_server == -1) {
        fprintf(stderr, "Socket creation error\n");
        return errno;
    }
    unlink(sockpath);
    return fd_sk_server;
}

int server_unix_bind(int fd_sk, char* path){
    struct sockaddr_un sa;
    strcpy(sa.sun_path, path);
    sa.sun_family = AF_UNIX;

    if((bind(fd_sk, (const struct sockaddr *) &sa, sizeof(sa)))==-1){
        fprintf(stderr, "bind() failed\n");
        if(errno == EADDRINUSE){
            fprintf(stderr, "Address already in use\n");
        }
        return errno;
    }
    int rc = listen(fd_sk, SOMAXCONN);
    if (rc < 0)
    {
       perror("listen() failed");
       return errno;
    }
    return 0;
}

int server_unix_accept(int fd_sk_server){

    int fd_sk_client = accept(fd_sk_server,NULL, 0);

    if (fd_sk_client < 0){
      perror("accept() failed");
    }

    return fd_sk_client;
}


int client_unix_socket(){
  int    sd = -1;
  //char   buffer[BUFFER_LENGTH];

  /********************************************************************/
  /* The socket() function returns a socket descriptor, which represents   */
  /* an endpoint.  The statement also identifies that the UNIX  */
  /* address family with the stream transport (SOCK_STREAM) will be   */
  /* used for this socket.                                            */
  /********************************************************************/
  sd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sd < 0){
     perror("socket() failed");
  }
  return sd;

}

int client_unix_connect(int sd, char* serverpath){
  /********************************************************************/
  /* If an argument was passed in, use this as the server, otherwise  */
  /* use the #define that is located at the top of this program.      */
  /********************************************************************/

  struct sockaddr_un serveraddr;
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sun_family = AF_UNIX;
  strcpy(serveraddr.sun_path, serverpath);

  /********************************************************************/
  /* Use the connect() function to establish a connection to the      */
  /* server.                                                          */
  /********************************************************************/
  int rc = connect(sd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
  if (rc < 0)
  {
     perror("connect() failed");
     return -1;
  }
  return rc;
}


/** Evita letture parziali
 *
 *   \retval -1   errore (errno settato)
 *   \retval  0   se durante la lettura da fd leggo EOF
 *   \retval size se termina con successo
 */
size_t readn(int fd, void *buf, size_t size) {
    size_t left = size;
    int r;
    void* bufptr = buf;
    while(left>0) {
        if ((r=(int) read(fd ,bufptr,left)) == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return 0;   // EOF
        left    -= r;
        bufptr  += r;
    }
    return size;
}

/** Evita scritture parziali
 *
 *   \retval -1   errore (errno settato)
 *   \retval  0   se durante la scrittura la write ritorna 0
 *   \retval  1   se la scrittura termina con successo
 */
int writen(int fd, void *buf, size_t size) {
    size_t left = size;
    int r;
    void* bufptr = buf;
    while(left>0) {
        if ((r=(int) write(fd ,bufptr,left)) == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return 0;
        left    -= r;
        bufptr  += r;
    }
    return 1;
}

int sendInteger(int fd_sk, size_t n) {
    if (writen(fd_sk, &n, sizeof(unsigned long)) == -1) {
        fprintf(stderr, "An error occurred while sending msg lenght\n");
        return errno;
    }

    return 0;
}

size_t receiveInteger(int fd_sk) {
    size_t n=0;
    if(readn(fd_sk, &n, sizeof(unsigned long)) == -1){
        fprintf(stderr, "An error occurred while reading msg lenght\n");
        return errno;
    }

    return n;
}

int sendn(int fd_sk, void* msg, size_t lenght){
    if(sendInteger(fd_sk,lenght)!=0){
        fprintf(stderr, "An error occurred on sending file size\n");
        return errno;
    }

    if(writen(fd_sk, msg, lenght) == -1){
        fprintf(stderr, "An error occurred on sending msg\n");
        return errno;
    }
    return 0;
}

/*
int sendfile(int fd_sk, const char* pathname){
    FILE* file= fopen(pathname,"rb");
    if(file==NULL){
        return -1;
    }

    size_t fsize= file_getsize(file);

    if(sendInteger(fd_sk,fsize)!=0){
        fprintf(stderr, "An error occurred on sending file size\n");
        return errno;
    }
    void* fcontent= file_readAll(file);
    if(fcontent==NULL){
        fclose(file);
        return -1;
    }

    if(writen(fd_sk, fcontent, fsize) == -1){
        fprintf(stderr, "An error occurred on sending file\n");
        exit(errno);
    }

    free(fcontent);
    return 0;
}

void receivefile(int fd_sk, void** buff, size_t* lenght){
    size_t size= receiveInteger(fd_sk);
    *lenght=size;

    *buff = malloc(size* sizeof(char));
    if(*buff==NULL){
        fprintf(stderr, "Could not receive a file: no storage left\n");
        exit(errno);
    }
    if(readn(fd_sk, *buff, size) == -1){
        fprintf(stderr, "An error occurred reading file\n");
        exit(errno);
    }
}*/

void sendStr(int to, char* msg){
    sendn(to,msg, (int)strlen(msg));
}

char* receiveStr(int from){
    size_t lenght = receiveInteger(from) * sizeof(char);

    char* buff= calloc(lenght+1, sizeof(char));
    if(buff == NULL){
        return NULL;
    }
    if(readn(from, buff, lenght) == -1){
        fprintf(stderr, "An error occurred on reading msg\n");
        return NULL;
    }
    buff[lenght]='\0';
    return (char*)buff;
}
