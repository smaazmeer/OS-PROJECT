#include "trading.h"
#include "account.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

void *price_updater(void *arg) {
    TradingSystem *system = (TradingSystem *)arg;
    
    // Seed random number generator with current time and thread ID
    srand(time(NULL) + pthread_self());
    
    while (*system->running) {
        usleep(500000); // Update every 0.5 seconds for smoother price movement
        
        for (int i = 0; i < NUM_STOCKS; i++) {
            double current = get_price(&system->stocks[i]);
            
            // Realistic random walk: small percentage change each update
            // Range: -0.5% to +0.5% per 0.5 second interval
            double percent_change = (rand() % 100 - 50) / 10000.0;
            double new_price = current * (1.0 + percent_change);
            
            // Keep prices realistic (at least $1, at most 10x the initial price)
            if (new_price < 1.0) new_price = 1.0;
            if (new_price > current * 2.0) new_price = current * 1.5; // Cap extreme jumps
            
            update_price(&system->stocks[i], new_price);
        }
    }
    return NULL;
}

void *order_processor(void *arg) {
    TradingSystem *system = (TradingSystem *)arg;
    
    while (*system->running) {
        // Use shorter timeout for responsive order processing
        Order order = dequeue(&system->queue);
        
        // Check if order is valid (not empty)
        if (order.quantity <= 0) continue;
        
        // Find stock
        Stock *stock = NULL;
        for (int i = 0; i < NUM_STOCKS; i++) {
            if (strcmp(system->stocks[i].name, order.stock_name) == 0) {
                stock = &system->stocks[i];
                break;
            }
        }
        if (!stock) continue;

        double price = get_price(stock);
        double cost = price * order.quantity;

        if (order.type == BUY) {
            if (get_balance(&system->account) >= cost) {
                update_balance(&system->account, -cost);
                add_holding(&system->account, order.stock_name, order.quantity, price);
                save_account_to_file(ACCOUNT_FILENAME, &system->account);
            }
        } else { // SELL
            if (get_holding(&system->account, order.stock_name) >= order.quantity) {
                update_balance(&system->account, cost);
                add_holding(&system->account, order.stock_name, -order.quantity, price);
                save_account_to_file(ACCOUNT_FILENAME, &system->account);
            }
        }
        
        // Small delay to prevent busy-waiting
        usleep(10000); // 10ms delay
    }
    return NULL;
}