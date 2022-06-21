/**************************************************************************/
/* This example program provides code for a server application that uses     */
/* AF_UNIX address family                                                 */
/**************************************************************************/

/**************************************************************************/
/* Header files needed for this sample program                            */
/**************************************************************************/
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "./lib/customsocket.h"

/**************************************************************************/
/* Constants used by this program                                         */
/**************************************************************************/
#define SERVER_PATH     "./socket/serversock"
#define BUFFER_LENGTH    250
#define FALSE              0

int main() {
  /***********************************************************************/
  /* Variable and structure definitions.                                 */
  /***********************************************************************/
  int    fd_sk_server = -1, fd_sk_client = -1;
  int    rc, length;
  //char   buffer[BUFFER_LENGTH];
  struct sockaddr_un serveraddr;

  fd_sk_server = server_unix_socket(SERVER_PATH);

  if(server_unix_bind(fd_sk_server, SERVER_PATH) != 0){
    perror("error during bind");
  }

  printf("Ready for client connect().\n");

  fd_sk_client = server_unix_accept(fd_sk_server);

  char *buffer;
  buffer = receiveStr(fd_sk_client);

  fprintf(stdout, "Messaggio ricevuto: %s\n", buffer);

  char *msg = "Informazioni";
  sendStr(fd_sk_client, msg);



  /***********************************************************************/
  /* Close down any open socket descriptors                              */
  /***********************************************************************/
  if (fd_sk_server != -1)
    close(fd_sk_server);

  if (fd_sk_client != -1)
    close(fd_sk_client);


  /***********************************************************************/
  /* Remove the UNIX path name from the file system                      */
  /***********************************************************************/
  unlink(SERVER_PATH);
}
