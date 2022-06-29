#include <stdbool.h>
#ifndef CUSTOM_QUEUE_H
#define CUSTOM_QUEUE_H

typedef struct queue_node{
    int value;
    struct queue_node *next;
} queue_node;

typedef struct queue {
    queue_node *head;
    queue_node *tail;
} queue;

queue *queue_create();
int queue_get(queue **q);
bool queue_isEmpty(queue* q);
void queue_insert(queue **q, int value);
void queue_destroy(queue **q);
void queue_close(queue **q);

#endif
