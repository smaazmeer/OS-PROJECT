#ifndef TRADING_H
#define TRADING_H

#include "stock.h"
#include "account.h"
#include "order_queue.h"
#include <pthread.h>

// Must match the number of init_stock() calls in main.c exactly
#define NUM_STOCKS          10
#define MAX_PENDING_ORDERS  100
#define MAX_HISTORY_ORDERS  500

typedef struct {
    Stock      stocks[NUM_STOCKS];
    Account    account;
    OrderQueue queue;

    Order            pending_orders[MAX_PENDING_ORDERS];
    int              pending_count;
    pthread_mutex_t  pending_mutex;

    Order            history[MAX_HISTORY_ORDERS];
    int              history_count;
    pthread_mutex_t  history_mutex;

    sem_t            session_sem;
    volatile int    *running;
} TradingSystem;

void *price_updater(void *arg);
void *order_processor(void *arg);
void *order_execution_thread(void *arg);

int  get_pending_orders(TradingSystem *system, Order *snapshot, int max_size);
int  cancel_pending_order(TradingSystem *system, int order_id);

void add_to_history(TradingSystem *system, Order order, OrderStatus final_status);
int  get_order_history(TradingSystem *system, Order *snapshot, int max_size);

int  place_secure_order(Account *acc, Order *new_order);
void process_cancel_order(Account *acc, int order_id);

#endif