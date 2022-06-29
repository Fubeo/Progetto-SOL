#include <stdbool.h>
#ifndef CUSTOM_STRING_H
#define CUSTOM_STRING_H

char* str_create(const char* s);
bool str_equals(const char* s1, const char* s2);
bool str_equals_ic(const char* s1, const char* s2);
char* str_concat(const char* s1, const char* s2);
char* str_concatn(const char* s1, ...);
int str_split(char*** output, const char* s, const char* delimiter);
int str_splitn(char*** output, const char* s, const char* delimiter, int n);
int str_starts_with(const char* s, const char* prefix);
bool str_ends_with(const char* s, const char* suffix);
bool str_is_empty(const char* s);
char* str_cut(const char* s, int from, int to);
void str_remove_new_line(char** s);
char* str_clean(char* s);
char* str_trim(char* s);  // rimuove spazi da s
char* str_long_toStr(long n);
int str_length(const char* s);
void str_clearArray(char*** array, const int lenght);
int str_toInteger(int* output, char* s);
#endif
