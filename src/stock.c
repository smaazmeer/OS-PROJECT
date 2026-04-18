#include "stock.h"
#include <string.h>

void init_stock(Stock *stock, const char *name, double initial_price) {
    strcpy(stock->name, name);
    stock->price = initial_price;
    stock->history_index = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        stock->history[i] = initial_price;
    }
    pthread_mutex_init(&stock->price_mutex, NULL);
}

void update_price(Stock *stock, double new_price) {
    pthread_mutex_lock(&stock->price_mutex);
    stock->price = new_price;
    stock->history[stock->history_index] = new_price;
    stock->history_index = (stock->history_index + 1) % HISTORY_SIZE;
    pthread_mutex_unlock(&stock->price_mutex);
}

double get_price(Stock *stock) {
    double price;
    pthread_mutex_lock(&stock->price_mutex);
    price = stock->price;
    pthread_mutex_unlock(&stock->price_mutex);
    return price;
}