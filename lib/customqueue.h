#include <stdbool.h>
#ifndef CUSTOM_QUEUE_H
#define CUSTOM_QUEUE_H

typedef struct node{
    int value;
    struct node *next;
} node;

typedef struct queue {
    node *head;
    node *tail;
} queue;

queue *queue_create();
int queue_get(queue **q);
bool queue_isEmpty(queue* q);
void queue_insert(queue **q, int value);
void queue_destroy(queue **q);
void queue_close(queue **q);

#endif
