#ifndef STOCK_H
#define STOCK_H

#include <pthread.h>

#define HISTORY_SIZE 100

typedef struct {
    char name[50];
    double price;
    double history[HISTORY_SIZE];
    int history_index;
    pthread_mutex_t price_mutex;
} Stock;

void init_stock(Stock *stock, const char *name, double initial_price);
void update_price(Stock *stock, double new_price);
double get_price(Stock *stock);

#endif