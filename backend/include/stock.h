#ifndef STOCK_H
#define STOCK_H

#include <pthread.h>

#define HISTORY_SIZE 100

// ADD THIS NEW STRUCT
typedef struct {
    double open;
    double high;
    double low;
    double close;
} Candle;

// UPDATE THE STOCK STRUCT
typedef struct {
    char name[50];
    double price;
    Candle history[HISTORY_SIZE];  // Changed from double to Candle
    int history_index;
    pthread_mutex_t price_mutex;
} Stock;

void init_stock(Stock *stock, const char *name, double initial_price);
void update_price(Stock *stock, double new_price);
double get_price(Stock *stock);

#endif