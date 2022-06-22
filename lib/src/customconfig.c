#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include "../customconfig.h"
#include "../customstring.h"
#include "../customprint.h"
#include "../customerrno.h"
#include "../customfile.h"

#define CHECK_ULONG_LIMIT(x) if((x)>ULONG_MAX){ \
        (x)=ULONG_MAX;\
    }

#define CHECK_UINT_LIMIT(x) if((x)>UINT_MAX){\
        (x)=UINT_MAX;\
    }

size_t convert_str(char *s) {
    char *type;
    size_t converted_s = strtol(s, &type, 10);
    type = str_clean(type);

    if (str_equals_ic(type, "m")) return converted_s * 1024 * 1024;
    else if (str_equals_ic(type, "g")) return converted_s * 1024 * 1024 * 1024;
    else if (str_is_empty(type)) return converted_s;

    errno = ERROR_CONV;
    return -1;
}

void settings_default(settings* s){
    s->N_WORKERS = 5;
    s->MAX_STORAGE = 209715200;
    s->MAX_STORABLE_FILES = 100;
    s->SOCK_PATH = str_create("mysock");
}

void setConfigfor(settings *s, char *key, char *value) {
    key = str_clean(key);
    int converted_v = 0;
    size_t sp;

    if (str_equals(key, "N_WORKERS")) {
        if (str_toInteger(&converted_v, value) != 0) {
            fprintf(stderr, "Error on parsing [%s]: default value set\n", key);
            return;
        }

        CHECK_UINT_LIMIT(converted_v)

        s->N_WORKERS = converted_v;
    } else if (str_equals(key, "MAX_STORAGE")) {
        errno = 0;
        sp = convert_str(value);
        if (errno == ERROR_CONV) {
            fprintf(stderr, "Error on parsing [%s]: default value set\n", key);
            return;
        }

        CHECK_ULONG_LIMIT(sp)

        s->MAX_STORAGE = sp;
    } else if (str_equals(key, "MAX_STORABLE_FILES")) {
        if (str_toInteger(&converted_v, value) != 0) {
            fprintf(stderr, "Error on parsing [%s]: default value set\n", key);
            return;
        }

        CHECK_UINT_LIMIT(converted_v)

        s->MAX_STORABLE_FILES = converted_v;
    } else if (str_equals(key, "SOCK_PATH")) {
        free(s->SOCK_PATH);
        char *new_path = str_clean(value);
        s->SOCK_PATH = str_create(new_path);
    }
}

void settings_load(settings *s, char *path) {
    FILE *c;

    if (path == NULL || str_is_empty(path)) {
        c = fopen("config.ini", "r");
    } else {
        c = fopen(path, "r");
    }
    if(c==NULL){
        settings_default(s);
        return;
    }

    if(s->SOCK_PATH==NULL){
        s->SOCK_PATH= str_create("mysock");
    }

    char **array = NULL;
    char *line;
    while ((line = file_readline(c)) != NULL) {
        char* cleaned_line = str_clean(line);
        if (!str_starts_with(cleaned_line, "#") && !str_is_empty(cleaned_line)) {
            int n=str_splitn(&array, cleaned_line, "=#", 3);
            setConfigfor(s, array[0], array[1]);

            str_clearArray(&array,n);
        }
        free(line);
    }

}

void settings_free(settings *s) {
    free(s->SOCK_PATH);
}

void settings_print(settings s) {
    fprintf(stdout, "MAX_STORABLE_FILES:\t\t\t");
    printf("%u\n", s.MAX_STORABLE_FILES);

    fprintf(stdout, "MAX_STORAGE (in bytes):\t\t");
    printf("%lu\n", s.MAX_STORAGE);

    fprintf(stdout, "N_WORKERS:\t\t\t");
    printf("%u\n", s.N_WORKERS);

    fprintf(stdout, "SOCK_PATH:\t\t\t\t");
    printf("%s\n", s.SOCK_PATH);

    //per rimuovere i warnings
    pwarn("");
    pcode(0, NULL);
    psucc("");
}
