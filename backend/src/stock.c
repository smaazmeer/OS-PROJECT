#include "stock.h"
#include <string.h>

void init_stock(Stock *stock, const char *name, double initial_price) {
    strcpy(stock->name, name);
    stock->price = initial_price;
    stock->history_index = 0;
    
    // Initialize all candles with the starting price
    for (int i = 0; i < HISTORY_SIZE; i++) {
        stock->history[i].open = initial_price;
        stock->history[i].high = initial_price;
        stock->history[i].low = initial_price;
        stock->history[i].close = initial_price;
    }
    pthread_mutex_init(&stock->price_mutex, NULL);
}

void update_price(Stock *stock, double new_price) {
    pthread_mutex_lock(&stock->price_mutex);
    stock->price = new_price;
    
    // Update the current candle
    Candle *current = &stock->history[stock->history_index];
    if (new_price > current->high) current->high = new_price;
    if (new_price < current->low) current->low = new_price;
    current->close = new_price;
    
    // For this simulation, we will move to a new candle every single tick
    // (In a real app, you would do this based on time, e.g., every 1 minute)
    stock->history_index = (stock->history_index + 1) % HISTORY_SIZE;
    
    // Setup the next candle
    Candle *next = &stock->history[stock->history_index];
    next->open = new_price;
    next->high = new_price;
    next->low = new_price;
    next->close = new_price;

    pthread_mutex_unlock(&stock->price_mutex);
}

double get_price(Stock *stock) {
    double price;
    pthread_mutex_lock(&stock->price_mutex);
    price = stock->price;
    pthread_mutex_unlock(&stock->price_mutex);
    return price;
}
