#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "../customsocket.h"
#include "../customqueue.h"
#include "../customconfig.h"
#include "../customerrno.h"
#include "../customlist.h"
#include "../customfile.h"
#include "../customhashtable.h"
#include "../customsortedlist.h"
#include "../serverfile.h"


file_s *file_init(char *path){
    if (str_is_empty(path))
        return NULL;

    file_s *file = malloc(sizeof(file_s));
    if (file == NULL) {
        perr("Unable to allocate file %s\n", (strrchr(path, '/') + 1));
        return NULL;
    }
    file->path = str_create(path);  //creo una copia del path per semplicità
    file->content = 0;
    file->size = 0;
    file->pidlist = list_create();

    file->mtx = malloc(sizeof(pthread_mutex_t));
    file->cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init (file->mtx, NULL);
    pthread_cond_init (file->cond, NULL);
    file->locked_by = NULL;

    pwarn("");pcode(0, NULL);psucc("");pcolor(STANDARD, ""); //per rimuovere i warnings

    return file;
  }


void file_open(hash_table **tbl_has_opened, file_s **f, char *cpid, bool requested_o_lock){

  list_insert(&(*f)->pidlist, cpid, NULL);

  int *n = (int *) hash_getValue(*tbl_has_opened, cpid);
  *n = *n + 1;

  hash_updateValue(tbl_has_opened, cpid, n, NULL);

  if(requested_o_lock){
    char *c = malloc(sizeof(char)*strlen(cpid) + 1);
    strcpy(c, cpid);
    (*f)->locked_by = c;
  }
}

bool file_isOpened(file_s *f){
    return !list_isEmpty(f->pidlist);
}

bool file_is_opened_by(file_s *f, char *pid){
    return list_contains_key(f->pidlist, pid);
}

bool file_is_empty(file_s *f) {
    return f->content == NULL;
}

void file_update(file_s **f, void *newContent, size_t newSize){
    if (newSize == 0) {   //se il file è vuoto
        free(newContent);
        return;
    }
    free((*f)->content);

    (*f)->content = newContent;
    (*f)->size = newSize;

}

int file_mtxWaitLock(file_s *f, hash_table *tbl_file_path, char* c, bool requested_o_lock){
  char *cpid;
  if(requested_o_lock){
    char *cpid = malloc(sizeof(char)*strlen(c) + 1);
    strcpy(cpid, c);
  } else cpid = c;
  char *filepath = malloc(sizeof(char)*strlen(f->path) + 1);
  strcpy(filepath, f->path);

  // Creazione di variabili utili a gestire una eventuale cancellazione del file da parte di altri client
  bool still_exists;
  pthread_cond_t *cond = f->cond;
  pthread_mutex_t *mtx = f->mtx;

  // Acquisizione della lock del semaforo del file. Chi non dietiene il flag o_lock (locked_by)
  // non esce dal ciclo finche' la lock non viene rilasciata.
  pthread_mutex_lock(mtx);
  while((still_exists = hash_containsKey(tbl_file_path, filepath)) && f->locked_by != NULL && strcmp(f->locked_by, cpid) != 0){
    if (pthread_cond_wait(cond, mtx) != 0) {  // attesa di una signal
      free(filepath);
      return COND_WAIT_ERROR; // non sono riuscito ad ottenere la lock
    }
  }
  if(!still_exists){ // Il worker controlla che il file non sia stato rimosso mentre aspettava la lock
    pthread_mutex_unlock(f->mtx);
    pthread_cond_broadcast(f->cond);
    free(filepath);
    return SFILE_WAS_REMOVED; // avevo ottenuto la lock del mutex del file, ma il file e' stato cancellato
  }
  if(requested_o_lock) f->locked_by = cpid;
  free(filepath);
  return S_SUCCESS; // ho ottenuto la lock del mutex del file
}

int file_mtxUnlockSignal(file_s *f, bool release_o_lock){
  if(release_o_lock && f->locked_by != NULL){
    free(f->locked_by);
    f->locked_by = NULL;
  }
  if(pthread_cond_signal(f->cond) != 0) return COND_SIGNAL_ERROR;
  if(pthread_mutex_unlock(f->mtx) != 0) return MTX_UNLOCK_ERROR;
  return S_SUCCESS;
}

int file_mtxUnlockBroadcast(file_s *f, bool release_o_lock){
  if(release_o_lock && f->locked_by != NULL){
      free(f->locked_by);
      f->locked_by = NULL;
  }
  if(pthread_cond_broadcast(f->cond) != 0) return COND_BROADCAST_ERROR;
  if(pthread_mutex_unlock(f->mtx) != 0) return MTX_UNLOCK_ERROR;
  return S_SUCCESS;
}

int file_mtxDeletedBroadcast(pthread_cond_t *cond, pthread_mutex_t *mtx){
  if(pthread_cond_broadcast(cond) != 0) return COND_BROADCAST_ERROR;
  if(pthread_mutex_unlock(mtx) != 0) return MTX_UNLOCK_ERROR;
  return S_SUCCESS;
}

void client_closes_file(hash_table **tbl_has_opened, file_s **f, char *cpid) {
  list_remove(&((*f)->pidlist), cpid, NULL);

  int *n = (int *) hash_getValue(*tbl_has_opened, cpid);
  *n -= 1;
  hash_updateValue(tbl_has_opened, cpid, n, NULL);

  assert((*(int *) hash_getValue(*tbl_has_opened, cpid)) >= 0);
}
