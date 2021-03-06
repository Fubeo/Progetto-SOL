#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "../customstring.h"
#include "../customlist.h"

list *list_create(){
    list* l = malloc(sizeof(list));
    if(l==NULL){
        fprintf(stderr, "Unable to create a list, malloc error\n");
        exit(errno);
    }
    l->head = NULL;
    l->tail = NULL;
    l->length = 0;

    return l;
}

void insert_head (list_node **head, char* key, void *value){
    list_node *element = (list_node *) malloc(sizeof(list_node));
    if(element==NULL){
        fprintf(stderr, "list malloc error: impossibile creare un nuovo nodo\n");
        exit(errno);
    }
    element->key = key;
    element->value = value;
    element->next = *head;

    *head = element;
}

void insert_tail (list_node **tail, char* key, void *value){
    list_node *element = (list_node *) malloc(sizeof(list_node));
    if(element==NULL){
        fprintf(stderr, "list malloc error: impossibile creare un nuovo nodo\n");
        exit(errno);
    }
    element->key=key;
    element->value = value;
    element->next = NULL;

    (*tail)->next = element;
    *tail = element;
}

bool list_remove(list **l, char* key, void (*delete_value)(void* value)){
    list_node** head=&(*l)->head;

    list_node *curr = *head;
    list_node *succ = (*head)->next;

    if (str_equals(curr->key, key)){
        *head = curr->next;
        free(curr->key);
        if(delete_value != NULL)
            delete_value(curr->value);
        free(curr);
        (*l)->length--;

        if((*l)->head==NULL){
            (*l)->tail=NULL;
        }
        return true;
    }

    if(succ == NULL){
        return false;
    }

    while (!str_equals(succ->key, key) && curr != NULL) {
        curr = curr->next;
        succ = succ->next;
        if(succ == NULL)
            break;
    }

    if(curr != NULL && succ == NULL) {
        if(str_equals(curr->key, key)) {
            free(curr->key);
            if(delete_value != NULL) delete_value(curr->value);
            free(curr);
            (*l)->length--;

            return true;
        }
        return false;
    }else if(curr != NULL){
        curr->next = succ->next;
        if(succ->next==NULL)    //se voglio eliminare l ultimo elemento
            (*l)->tail=curr;

        free(succ->key);
        if(delete_value != NULL)
            delete_value(succ->value);
        free(succ);
        (*l)->length--;
        return true;
    }

    return false;
}

bool list_isEmpty(list* l){
    return l->length==0 || l->head==NULL;
}

void list_destroy(list** l, void (*delete_value)(void* value)){
    while((*l)->head != NULL){
        list_node* curr=(*l)->head;
        free(curr->key);
        if(delete_value != NULL)
            delete_value(curr->value);
        (*l)->head=curr->next;
        free(curr);
    }

    free((*l));
}

void list_insert(list **l, char* key, void *value) {
    char* dup_key = malloc(sizeof(char) * strlen(key) + 1);
    strcpy(dup_key, key);

    if ((*l)->head == NULL) {
        insert_head(&(*l)->head, dup_key, value);
        (*l)->tail = (*l)->head;
    } else {
        insert_tail(&(*l)->tail, dup_key, value);
    }
    (*l)->length++;
}

list_node* list_getNode(list* l, char* key){
    list_node* head = l->head;
    list_node* tail=l->tail;
    if(head==NULL){
        return NULL;
    }

    if(str_equals(head->key,key)){
        return head;
    }else if(str_equals(tail->key,key)){
        return tail;
    }

    while (head != NULL && !str_equals(head->key,key)) {
        head = head->next;
    }

    if(head != NULL){
        return head;
    }

    return NULL;
}

int list_getlength(list* l){
    return l->length;
}

bool list_contains_key(list* l, char* key){
    return list_getNode(l,key) != NULL;
}
