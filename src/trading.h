#ifndef TRADING_H
#define TRADING_H

#include "stock.h"
#include "account.h"
#include "order_queue.h"

#define NUM_STOCKS 10

typedef struct {
    Stock stocks[NUM_STOCKS];
    Account account;
    OrderQueue queue;
    sem_t session_sem;      // Limits concurrent active trading sessions
    volatile int *running;
} TradingSystem;

void *price_updater(void *arg);
void *order_processor(void *arg);

#endif