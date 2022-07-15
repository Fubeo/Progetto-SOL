#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <string.h>
#include "../customlog.h"
#include "../customstring.h"

#define BUFSIZE 300

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
      "%s%4d-%02d-%02d|%02d:%02d:%02d.log",
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
  snprintf(s, BUFSIZE, "Worker %d: Managing a %s operation for client with pid %s connected at socket %d",
    gettid(),
    operation,
    cpid,
    fd_c
  );
  log_addline(lf, s);
  free(s);
}

void log_addrequest(logfile *lf, char *request){
  char *t = str_long_toStr(gettid());
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %s received a new request: %s", t, request);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addcloseconnection(logfile *lf, char *cpid){
  char *s = str_concatn("Client ", cpid, " disconnected", NULL);
  log_addline(lf, s);
  free(s);
}

void log_addread(logfile *lf, char *cpid, char *pathname, size_t size){
  char *t = str_long_toStr(gettid());
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %s: Client %s has read file %s of size [%ld]", t, cpid, pathname, size);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addwrite(logfile *lf, char *cpid, char *pathname, size_t size){
  char *t = str_long_toStr(gettid());
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %s: Client %s has written file %s of size [%ld]", t, cpid, pathname, size);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addappend(logfile *lf, char *cpid, char *pathname, size_t size){
  char *t = str_long_toStr(gettid());
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %s: Client %s has appended to file %s a file of size [%ld]", t, cpid, pathname, size);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addcreate(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " has created file ", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addopen(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " has opened file ", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addcreatelock(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " has created with lock file", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addopenlock(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " has opened with lock file ", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addeject(logfile *lf, char *cpid, char *pathname, size_t size){
  char *t = str_long_toStr(gettid());
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %s: Client %s ejected file %s of size [%ld]", t, cpid, pathname, size);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addremove(logfile *lf, char *cpid, char *pathname, size_t size){
  char *t = str_long_toStr(gettid());
  char *s = malloc(BUFSIZE*sizeof(char));
  snprintf(s, BUFSIZE, "Worker %s: Client %s removed file %s of size [%ld]", t, cpid, pathname, size);
  log_addline(lf, s);
  free(s);
  free(t);
}

void log_addclose(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " closed file ", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addunlock(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " unlocked file ", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addlock(logfile *lf, char *cpid, char *pathname){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Client ", cpid, " locked file ", pathname, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_adderror(logfile *lf, char *cpid, char *msg){
  char *t = str_long_toStr(gettid());
  char *s = str_concatn("Worker ", t, ": Error with client ", cpid, ": ", msg, NULL);
  log_addline(lf, s);
  free(t);
  free(s);
}

void log_addStats(logfile *lf, size_t msds, size_t msf, int mcc){
  log_addline(lf, "\nSTATISTICS:");

  char *n = str_long_toStr(msds);
  char *s = str_concatn("Max stored data size (MB): ", n, "", NULL);
  log_addline(lf, s);
  free(n);
  free(s);

  n = str_long_toStr(msf);
  s = str_concatn("Max number of files stored: ", n, NULL);
  log_addline(lf, s);
  free(n);
  free(s);

  n = str_long_toStr(mcc);
  s = str_concatn("Max connected clients: ", n, NULL);
  log_addline(lf, s);
  free(n);
  free(s);
}

void log_free(logfile *lf){
  fclose(lf->file);
  pthread_mutex_destroy(lf->log_mtx);
  free(lf->log_mtx);
  free(lf);
}
