#include <stdbool.h>
#include <pthread.h>
#ifndef SERVER_FILE_H
#define SERVER_FILE_H

typedef struct {
    char *path;
    void *content;
    size_t size;
    list *pidlist;

    pthread_cond_t *cond;
    pthread_mutex_t *mtx;
    char* locked_by;
} file_s;


file_s *file_init(char *path);

// Funzione che simula l apertura del file f da parte del client cpid.
void file_open(hash_table **tbl_has_opened, file_s **f, char *cpid, bool requested_o_lock);

bool file_isOpened(file_s *f);

// Ritorna true se il file f è aperto dal Client pid, false altrimenti.
bool file_is_opened_by(file_s *f, char *pid);

// Ritorna true se il file f è vuoto, false altrimenti.
bool file_is_empty(file_s *f);

// Funzione che aggiorna il contenuto e la grandezza di un file.
void file_update(file_s **f, void *newContent, size_t newSize);

int file_mtxWaitLock(file_s *f, hash_table *tbl_file_path, char* cpid, bool requested_o_lock);

int file_mtxUnlockSignal(file_s *f, bool release_o_lock);

int file_mtxUnlockBroadcast(file_s *f, bool release_o_lock);

int file_mtxDeletedBroadcast(pthread_cond_t *cond, pthread_mutex_t *mtx);


// Funzione che simula la chiusura del file f da parte del Client cpid.
void client_closes_file(hash_table **tbl_has_opened, file_s **f, char *cpid);




#endif
