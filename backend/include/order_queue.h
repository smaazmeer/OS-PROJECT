#ifndef ORDER_QUEUE_H
#define ORDER_QUEUE_H

#include "order.h"
#include <pthread.h>
#include <semaphore.h>

#define QUEUE_SIZE 100

typedef struct {
    Order orders[QUEUE_SIZE];
    int front;
    int rear;
    int count;
    pthread_mutex_t mutex;
    sem_t empty;
    sem_t full;
} OrderQueue;

void init_queue(OrderQueue *queue);
void enqueue(OrderQueue *queue, Order order);
Order dequeue(OrderQueue *queue);
int queue_snapshot(OrderQueue *queue, Order *snapshot, int max_size);
void cancel_order_in_queue(OrderQueue *queue, int order_id);

#endif