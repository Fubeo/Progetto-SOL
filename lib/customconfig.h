#define DEFAULT_SETTINGS { .N_WORKERS = 5, .MAX_STORAGE = 209715200, .MAX_STORABLE_FILES = 100, .SOCK_PATH = NULL, .PRINT_LOG = 1}

#define ERROR_CONV 1
typedef struct{
    unsigned int N_WORKERS;
    size_t MAX_STORAGE;
    unsigned int MAX_STORABLE_FILES;
    int PRINT_LOG;
    char* SOCK_PATH;
} settings;

#ifndef CUSTOM_CONFIG_H
#define CUSTOM_CONFIG_H
void settings_free(settings* s);
void settings_load(settings* s, char* path);
void settings_default(settings* s);
void settings_print(settings s);
#endif
