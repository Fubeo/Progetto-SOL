#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include "../customstring.h"
#include "../customfile.h"

char* file_readline(FILE* file, char **buffer, int len){
  char* ret = fgets(*buffer, len, file);
  str_remove_new_line(buffer);
  return ret;
}

void *file_read_all(FILE* file){
    size_t file_size = file_getsize(file);

    void* buffer= malloc(sizeof(char) * file_size);

    if(buffer == NULL){
        fprintf(stderr, "Impossibile allocare spazio: file_read_all() malloc error\n");
        return NULL;
    }
    fread(buffer, sizeof(char), file_size, file);

    return buffer;
}

size_t file_getsize(FILE* file){
    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);
    return size;
}

void file_close(FILE* fp){
    fclose(fp);
}

bool is_directory(const char *file)
{
    struct stat p;
    stat(file, &p);
    return S_ISDIR(p.st_mode);
}

int file_scanAllDir(char*** output, char* init_dir){
    return file_nscan(output,init_dir,-1);
}

int file_nscan(char*** output, char* init_dir, int left_to_read){
  *output = calloc(2, sizeof(char*));
  int array_size = 2;
  int current_length=0;
  return file_nscanAllDir(output, init_dir, &left_to_read, &array_size, &current_length);
}

int file_nscanAllDir(char*** output, char* init_dir, int *left_to_read, int *array_size, int *current_length){

  DIR *current_dir = opendir(init_dir);

  if (current_dir == NULL || *left_to_read==0)
  {
      return *current_length;
  }

  struct dirent *file;

  while ((file = readdir(current_dir)) != NULL) {
    if(*left_to_read == 0)
        break;

    if(*current_length==*array_size){
        *array_size *= 2;
        *output = realloc(*output, (*array_size) * sizeof(char*));
    }
    char *file_name = file->d_name;

    if (strcmp(".", file_name) != 0 && strcmp("..", file_name) != 0 && file_name[0] != '.') {

      if(str_ends_with(init_dir, "/")) file_name = str_concatn(init_dir, file_name, NULL);
      else file_name = str_concatn(init_dir,"/",file_name,NULL);

      char* filepath = realpath(file_name,NULL);
     
      assert(filepath != NULL);

      if(!is_directory(filepath)) {
         (*output)[*current_length] = filepath;
         (*current_length)++;
         (*left_to_read)--;
         free(file_name);
      } else {
         file_nscanAllDir(output, filepath, left_to_read, array_size, current_length);
      }
    }
  }

  closedir(current_dir);
  return *current_length;
}
