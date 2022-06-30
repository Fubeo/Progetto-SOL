// Definizione codici identificativi di apertura del file


#ifndef CLIENT_FILE_MANAGER_H
#define CLIENT_FILE_MANAGER_H

int readFile(const char* pathname, void** buf, size_t* size);
int readNFiles(int N, const char *dirname);
int writeFile(const char* pathname, const char* dirname);
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);
int removeFile(const char* pathname);
#endif
