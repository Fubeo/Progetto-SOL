#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "../customlog.h"
#include "../customstring.h"

#ifndef SYS_gettid
#error "SYS_gettid unavailable on this system"
#endif

#define gettid() ((pid_t)syscall(SYS_gettid))

logfile* log_init(char *logsdir){
  char *path = generate_logpath(logsdir);
  logfile *lf = malloc(sizeof(logfile));

  lf->file = fopen(path, "wa");
  if(lf->file == NULL){
    perror("Failed to open log file");
  }

  free(path);

  lf->log_mtx = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init (lf->log_mtx, NULL);

  return lf;
}

char *generate_logpath(char *logsdir){
  struct tm curr_time;
  char* log_file_path;
  time_t curr_time_abs = time(NULL);

  localtime_r(&curr_time_abs, &curr_time);
  int dir_path_len = strlen(logsdir);

  log_file_path = malloc((dir_path_len + 30) * sizeof(char));
  memset(log_file_path, '\0', sizeof(char)*(dir_path_len + 30));

  snprintf(
      log_file_path, dir_path_len + 30,
      "%s%4d-%02d-%02d|%02d:%02d:%02d.txt",
      logsdir,
      curr_time.tm_year + 1900,
      curr_time.tm_mon + 1,
      curr_time.tm_mday,
      curr_time.tm_hour,
      curr_time.tm_min,
      curr_time.tm_sec
  );

  return log_file_path;
}

void log_addline(logfile *lf, char *s){
  pthread_mutex_lock(lf->log_mtx);
  fprintf(lf->file, "%s\n", s);
  pthread_mutex_unlock(lf->log_mtx);
}

void log_addreadablerequest(logfile *lf, char *operation, char *cpid, int fd_c){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Managing a %s operation for client with pid %s connected to socket %d",
    gettid(),
    operation,
    cpid,
    fd_c
  );
  log_addline(lf, s);
  free(s);
}

void log_addrequest(logfile *lf, char *request){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d received a new request: %s", gettid(), request);
  log_addline(lf, s);
  free(s);
}

void log_addcloseconnection(logfile *lf, char *cpid){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Client %s disconnected", cpid);
  log_addline(lf, s);
  free(s);
}

void log_addread(logfile *lf, char *cpid, char *pathname, size_t size){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has read file %s of size [%ld]", gettid(), cpid, pathname, size);
  log_addline(lf, s);
  free(s);
}

void log_addwrite(logfile *lf, char *cpid, char *pathname, size_t size, int opened_files){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has written file %s of size [%ld]", gettid(), cpid, pathname, size);
  log_addline(lf, s);
  if(opened_files == 1) snprintf(s, BUFSIZE, "Client %s has 1 file opened", cpid);
  else snprintf(s, BUFSIZE, "Client %s has %d files opened", cpid, opened_files);
  log_addline(lf, s);
  free(s);
}

void log_addappend(logfile *lf, char *cpid, char *pathname, size_t size){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has appended to file %s a file of size [%ld]", gettid(), cpid, pathname, size);
  log_addline(lf, s);
  free(s);
}

void log_addcreate(logfile *lf, char *cpid, char *pathname){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has created file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  free(s);
}

void log_addopen(logfile *lf, char *cpid, char *pathname, int opened_files){

  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has opened file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  if(opened_files == 1) snprintf(s, BUFSIZE, "Client %s has 1 file opened", cpid);
  else snprintf(s, BUFSIZE, "Client %s has %d files opened", cpid, opened_files);
  log_addline(lf, s);
  free(s);

}

void log_addcreatelock(logfile *lf, char *cpid, char *pathname){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has created with lock file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  free(s);
}

void log_addopenlock(logfile *lf, char *cpid, char *pathname, int opened_files){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s has opened with lock file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  if(opened_files == 1)snprintf(s, BUFSIZE, "Client %s has 1 file opened", cpid);
  else snprintf(s, BUFSIZE, "Client %s has %d files opened", cpid, opened_files);
  log_addline(lf, s);
  free(s);
}

void log_addeject(logfile *lf, char *cpid, char *pathname, size_t size){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s ejected file %s of size [%ld]", gettid(), cpid, pathname, size);
  log_addline(lf, s);
  free(s);
}

void log_addremove(logfile *lf, char *cpid, char *pathname, size_t size){
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s removed file %s of size [%ld]", gettid(), cpid, pathname, size);
  log_addline(lf, s);
  free(s);
}

void log_addclose(logfile *lf, char *cpid, char *pathname, int opened_files){
  char *s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s closed file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  if(opened_files == 1)snprintf(s, BUFSIZE, "Client %s has 1 file opened", cpid);
  else snprintf(s, BUFSIZE, "Client %s has %d files opened", cpid, opened_files);
  log_addline(lf, s);
  free(s);
}

void log_addunlock(logfile *lf, char *cpid, char *pathname){
  char *s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s unlocked file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  free(s);
}

void log_addlock(logfile *lf, char *cpid, char *pathname){
  char *s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Client %s locked file %s", gettid(), cpid, pathname);
  log_addline(lf, s);
  free(s);
}

void log_adderror(logfile *lf, char *cpid, char *msg){
  char *s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Worker %d: Error with client %s: %s", gettid(), cpid, msg);
  log_addline(lf, s);
  free(s);
}

void log_addStats(logfile *lf, size_t msds, size_t msf, int mcc){
  log_addline(lf, "\nSTATISTICS:");

  char *s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Max stored data size (MB): %ld", msds);
  log_addline(lf, s);
  free(s);

  s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Max number of files stored: %ld", msf);
  log_addline(lf, s);
  free(s);

  s = malloc(BUFSIZE * sizeof(char));
  snprintf(s, BUFSIZE, "Max connected clients: %d", mcc);
  log_addline(lf, s);
  free(s);

}

void log_free(logfile *lf){
  fclose(lf->file);
  pthread_mutex_destroy(lf->log_mtx);
  free(lf->log_mtx);
  free(lf);
}
