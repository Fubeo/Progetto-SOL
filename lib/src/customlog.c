#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include "../customlog.h"
#include "../customstring.h"


char *current_time(){
  struct sysinfo info;
  sysinfo(&info);
  const time_t boottime = time(NULL) - info.uptime;

  struct timespec monotime;
  clock_gettime(CLOCK_MONOTONIC, &monotime);
  time_t curtime = boottime + monotime.tv_sec;
  return ctime(&curtime);
}

logfile* log_init(char *logsdir){
  char *stime = current_time();
  char *path = str_concatn(logsdir, stime,".log", NULL);
  logfile *lf = malloc(sizeof(logfile));

  lf->file = fopen(path, "wa");
  free(path);

  lf->log_mtx = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init (lf->log_mtx, NULL);

  return lf;
}

void log_addline(logfile *lf, char *line){
  pthread_mutex_lock(lf->log_mtx);
  fputs(line, lf->file);
  putc('\n', lf->file);
  pthread_mutex_unlock(lf->log_mtx);
}

void log_addrequest(logfile *lf, char *request){
  char *s = str_concatn("New request -> ", request, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addcloseconnection(logfile *lf, char *cpid){
  char *s = str_concatn("Client ", cpid, " disconnected", NULL);
  log_addline(lf, s);
  free(s);
}

void log_addread(logfile *lf, char *cpid, char *pathname, size_t size){
  char *sz = str_long_toStr(size);
  char *s = str_concatn("Client ", cpid, " has read file ", pathname, " of size ", sz, NULL);
  log_addline(lf, s);
  free(sz);
  free(s);
}

void log_addwrite(logfile *lf, char *cpid, char *pathname, size_t size){
  char *sz = str_long_toStr(size);
  char *s = str_concatn("Client ", cpid, " has written file ", pathname, " of size ", sz, NULL);
  log_addline(lf, s);
  free(sz);
  free(s);
}

void log_addcreate(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " has created file ", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addopen(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " has opened file ", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addcreatelock(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " has created with lock file", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addopenlock(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " has opened with lock file ", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addeject(logfile *lf, char *cpid, char *pathname, size_t size){
  char *sz = str_long_toStr(size);
  char *s = str_concatn("Client ", cpid, " ejected file ", pathname, " of size ", sz, NULL);
  log_addline(lf, s);
  free(sz);
  free(s);
}

void log_addremove(logfile *lf, char *cpid, char *pathname, size_t size){
  char *sz = str_long_toStr(size);
  char *s = str_concatn("Client ", cpid, " removed file ", pathname, " of size ", sz, NULL);
  log_addline(lf, s);
  free(sz);
  free(s);
}

void log_addclose(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " closed file ", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addunlock(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " unlocked file ", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_addlock(logfile *lf, char *cpid, char *pathname){
  char *s = str_concatn("Client ", cpid, " locked file ", pathname, NULL);
  log_addline(lf, s);
  free(s);
}

void log_adderror(logfile *lf, char *cpid, char *msg){
  char *s = str_concatn("Error with client ", cpid, ": ", msg, NULL);
  log_addline(lf, s);
  free(s);
}

void log_free(logfile *lf){
  fclose(lf->file);
  pthread_mutex_destroy(lf->log_mtx);
  free(lf->log_mtx);
  free(lf);
}
