#define _GNU_SOURCE
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 500

#ifndef CUSTOM_LOG_H
#define CUSTOM_LOG_H

typedef struct{
  FILE *file;
  pthread_mutex_t *log_mtx;
} logfile;

logfile* log_init(char *logsdir);
char *generate_logpath(char *logsdir);
void log_addline(logfile *lf, char *s);
void log_addrequest(logfile *lf, char *request);
void log_addreadablerequest(logfile *lf, char *operation, char *cpid, int fd_c);
void log_addcloseconnection(logfile *lf, char *cpid);
void log_addread(logfile *lf, char *cpid, char *pathname, size_t size);
void log_addwrite(logfile *lf, char *cpid, char *pathname, size_t size, int opened_files);
void log_addappend(logfile *lf, char *cpid, char *pathname, size_t size);
void log_addcreate(logfile *lf, char *cpid, char *pathname);
void log_addopen(logfile *lf, char *cpid, char *pathname, int opened_files);
void log_addcreatelock(logfile *lf, char *cpid, char *pathname);
void log_addopenlock(logfile *lf, char *cpid, char *pathname, int opened_files);
void log_addeject(logfile *lf, char *cpid, char *pathname, size_t size);
void log_addremove(logfile *lf, char *cpid, char *pathname, size_t size);
void log_addclose(logfile *lf, char *cpid, char *pathname, int opened_files);
void log_addunlock(logfile *lf, char *cpid, char *pathname);
void log_addlock(logfile *lf, char *cpid, char *pathname);
void log_adderror(logfile *lf, char *cpid, char *msg);
void log_addStats(logfile *lf, size_t msds, size_t msf, int mcc);
void log_free(logfile *lf);

#endif
