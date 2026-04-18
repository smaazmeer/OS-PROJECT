#include "order_queue.h"

void init_queue(OrderQueue *queue) {
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    sem_init(&queue->empty, 0, QUEUE_SIZE);
    sem_init(&queue->full, 0, 0);
}

void enqueue(OrderQueue *queue, Order order) {
    sem_wait(&queue->empty);
    pthread_mutex_lock(&queue->mutex);
    queue->orders[queue->rear] = order;
    queue->rear = (queue->rear + 1) % QUEUE_SIZE;
    queue->count++;
    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->full);
}

Order dequeue(OrderQueue *queue) {
    sem_wait(&queue->full);
    pthread_mutex_lock(&queue->mutex);
    Order order = queue->orders[queue->front];
    queue->front = (queue->front + 1) % QUEUE_SIZE;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->empty);
    return order;
}

int queue_snapshot(OrderQueue *queue, Order *snapshot, int max_size) {
    pthread_mutex_lock(&queue->mutex);
    int count = queue->count;
    if (count > max_size) count = max_size;
    for (int i = 0; i < count; i++) {
        int index = (queue->front + i) % QUEUE_SIZE;
        snapshot[i] = queue->orders[index];
    }
    pthread_mutex_unlock(&queue->mutex);
    return count;
}