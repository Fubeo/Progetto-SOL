#include <errno.h>
#include <stdarg.h>
#include "customprint.h"

#define S_SUCCESS 0
#define SFILE_ALREADY_EXIST 1
#define SFILE_NOT_FOUND 2
#define SFILE_ALREADY_OPENED 3
#define SFILE_NOT_OPENED 4
#define SFILE_NOT_EMPTY 5
#define S_STORAGE_EMPTY 6
#define SFILES_FOUND_ON_EXIT 7
#define SOCKET_ALREADY_CLOSED 8
#define S_STORAGE_FULL 9    //non è un errore, in quanto se il Server è pieno, dei file vengono rimossi
#define EOS_F 10 //end-of-stream-files
#define HASH_NULL_PARAM 11
#define HASH_INSERT_SUCCESS 12
#define HASH_DUPLICATE_KEY 13
#define HASH_KEY_NOT_FOUND 14
#define SFILE_TOO_LARGE 15
#define SFILE_OPENED 16
#define CONNECTION_TIMED_OUT 17
#define WRONG_SOCKET 18
#define FILE_NOT_FOUND 19
#define INVALID_ARG 20
#define S_FREE_ERROR 21
#define CONNECTION_REFUSED 22
#define CONNECTION_ACCEPTED 23
#define MALLOC_ERROR 24
#define SFILE_LOCKED 25
#define SFILE_NOT_LOCKED 26
#define CLIENT_NOT_ALLOWED 27
#define MTX_LOCK_ERROR 28
#define MTX_UNLOCK_ERROR 29
#define COND_SIGNAL_ERROR 30
#define COND_WAIT_ERROR 31
#define COND_BROADCAST_ERROR 32
#define SFILE_WAS_REMOVED 33

#ifndef PROGETTO_CUSTOMERRNO_H
#define PROGETTO_CUSTOMERRNO_H

static void pcode(int code, char* file) {
    if(code == S_SUCCESS)
        return;

    if(file == NULL)
        file = "(null)";

    switch (code) {
        case SFILE_ALREADY_EXIST : {
            perr("ERROR: file %s already stored on the server\n"
                            "Errcode: SFILE_ALREADY_EXIST\n\n", file);
            break;
        }

        case SFILE_NOT_FOUND : {
            perr("ERRORE: file %s not stored on the erver\n"
                            "Errcode: SFILE_NOT_FOUND\n\n", file);
            break;
        }

        case SFILE_ALREADY_OPENED : {
            printf(YEL "WARNING: file %s already opened\n"
                   "Errcode: SFILE_ALREADY_OPENED\n\n", file);
            break;
        }
        case SFILE_NOT_OPENED : {
            perr("ERRORE: file %s not opened\n"
                  "Write not permitted on non-opened files\n"
                  "Errcode: SFILE_NOT_OPENED\n\n", file);
            break;
        }

        case SFILE_NOT_EMPTY : {
            perr("ERRORE: write not permitted on empty files\n"
                            "File: %s\n"
                            "Errcode: SFILE_NOT_EMPTY\n\n", file);
            break;
        }

        case S_STORAGE_EMPTY : {
            printf(YEL "WARNING: server is empty\n"
                   "Errcode: S_STORAGE_EMPTY\n\n");
            break;
        }

        case SOCKET_ALREADY_CLOSED : {
            perr("ERRORE: socket is already closed\n"
                 "Errcode: SOCKET_ALREADY_CLOSED\n\n");
            break;
        }

        case SFILE_TOO_LARGE : {
            perr("ERRORE: file %s is too large\n"
                            "Errcode: SFILE_TOO_LARGE\n\n", file);
            break;
        }

        case CONNECTION_TIMED_OUT : {
            perr("ERRORE: unable to connect to the server\n"
                            "Errcode: CONNECTION_TIMED_OUT\n\n");
            break;
        }

        case WRONG_SOCKET : {
            perr("ERRORE: socket passed as argument is not the same which the client is connected to\n"
                            "Errcode: WRONG_SOCKET\n\n");
            break;
        }

        case FILE_NOT_FOUND : {
            perr("ERRORE: file %s not found\n"
                            "Errcode: FILE_NOT_FOUND\n\n", file);
            break;
        }

        case INVALID_ARG : {
            perr("ERRORE: invalid argument\n"
                            "Errcode: INVALID_ARG\n\n");
            break;
        }

        case S_FREE_ERROR : {
            perr("ERRORE: unable to free storage on the server\n"
                            "Errcode: S_FREE_ERROR\n\n");
            break;
        }

        case CONNECTION_REFUSED : {
            perr("ERRORE: unable to connect to the server\n"
                            "Errcode: CONNECTION_REFUSED\n\n");
            break;
        }
        case MALLOC_ERROR : {
            perr("ERRORE: malloc failed, try to remove some files\n"
                 "Errcode: MALLOC_ERROR\n\n");
            break;
        }

        case SFILE_LOCKED : {
            perr("ERROR: operation not permitted on locked file %s\n"
                            "Errcode: SFILE_ALREADY_EXIST\n\n", file);
            break;
        }


        default: {
            break;
        }
    }
}

#undef GRN
#undef YEL
#undef WHT
#undef RD
#undef RESET
#endif
