#define O_OPEN 0
#define O_CREATE 1
#define O_LOCK 2

#ifndef CLIENT_API_H
#define CLIENT_API_H

int openConnection(const char *sockname, int msec, const struct timespec abstime);
int closeConnection(const char *sockname);
void exit_function();

int openFile(char *pathname, int flags);
int closeFile(const char *pathname);

// -w e -W
int writeFile(const char *pathname, const char *dirname);
void send_file_to_server(const char *backup_folder, char *file);

// -a
int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname);

// -R
int readNFiles(int N, const char *dirname);

// -r
int readFile(const char *pathname, void **buf, size_t *size);

// -c
int removeFile(const char *pathname);

// -l
int lockFile(const char*pathname);

// -u
int unlockFile(const char*pathname);

#endif
