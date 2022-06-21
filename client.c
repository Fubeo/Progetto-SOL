/**************************************************************************/
/* This sample program provides code for a client application that uses     */
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
#define SERVER_PATH "./socket/serversock"
#define BUFFER_LENGTH    250
#define FALSE              0

/* Pass in 1 parameter which is either the */
/* path name of the server as a UNICODE    */
/* string, or set the server path in the   */
/* #define SERVER_PATH which is a CCSID    */
/* 500 string.                             */
int main(int argc, char *argv[])
{
  int sd = client_unix_socket();
  int rc = client_unix_connect(sd, SERVER_PATH);

  // client - server
  char *buffer = "Ho bisogno di Informazioni";
  sendStr(sd, buffer);

  char *msg;
  msg = receiveStr(sd);
  fprintf(stdout, "Messaggio ricevuto: %s\n", msg);

  /***********************************************************************/
  /* Close down any open socket descriptors                              */
  /***********************************************************************/
  if (sd != -1)
  close(sd);
}
