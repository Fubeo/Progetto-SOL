#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "./lib/customsocket.h"

#define SERVER_PATH "./tmp/serversock.sk"
#define BUFFER_LENGTH    250
#define FALSE              0


int main(int argc, char *argv[])
{
  int sd = client_unix_socket();
  client_unix_connect(sd, SERVER_PATH);

  // client - server
  sendStr(sd, argv[1]);

  char *msg;
  msg = receiveStr(sd);
  fprintf(stdout, "Server: %s\n", msg);

  if (sd != -1)
  close(sd);
}
