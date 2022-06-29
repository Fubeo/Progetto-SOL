#ifndef CUSTOM_LIST_H
#define CUSTOM_LIST_H
#include <stdbool.h>

typedef struct list_node {
    char* key;
    void *value;
    struct list_node *next;
} list_node;

typedef struct list {
    list_node *head;
    list_node *tail;
    int length;
} list;

list *list_create();
bool list_remove(list **l, char* key, void (*delete_value)(void* value));
bool list_isEmpty(list* l);
list_node* list_getNode(list* l, char* key);
void list_insert(list **l, char* key, void *value);
void list_destroy(list** l, void (*delete_value)(void* value));
bool list_contains_key(list* l, char* key);
#endif
