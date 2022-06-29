#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "../customstring.h"
#include "../customprint.h"

void pcolor(enum Color c, char* s, ...){
    va_list argp;
    va_start(argp, s);
    char* p;
    switch (c) {
        case RED:
            p = str_concat(RD,s);
            break;
        case GREEN:
            p = str_concat(GRN,s);
            break;
        case YELLOW:
            p = str_concat(YEL,s);
            break;
        case BLUE:
            p = str_concat(BLU,s);
            break;
        case MAGENTA:
            p = str_concat(MAG,s);
            break;
        case CYAN:
            p = str_concat(CYN,s);
            break;
        case WHITE:
            p = str_concat(WHT,s);
            break;
        case STANDARD:
            p = str_concat(RESET,s);
            break;
    }

    vprintf(p, argp);
    va_end(argp);
    free(p);
    printf(RESET);
}


void psucc(char *s, ...) {
  va_list argp;
  va_start(argp, s);
  char* p;
  p = str_concat(GRN,s);
  vprintf(p, argp);
  va_end(argp);
  free(p);
  printf(RESET);
}

void pwarn(char* s,...){
  va_list argp;
  va_start(argp, s);
  char* p;
  p = str_concat(YEL,s);
  vprintf(p, argp);
  va_end(argp);
  free(p);
  printf(RESET);
}

void perr(char* s,...){
  va_list argp;
  va_start(argp, s);
  char* p;
  p = str_concat(RD,s);
  vprintf(p, argp);
  va_end(argp);
  free(p);
  printf(RESET);
}
