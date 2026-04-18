#include "trading.h"
#include "ui.h"
#include <pthread.h>
#include <stdlib.h>

int main() {
    volatile int running = 1;
    TradingSystem system = {.running = &running};

    // Initialize stocks
    init_stock(&system.stocks[0], "AAPL", 150.0);
    init_stock(&system.stocks[1], "GOOG", 2800.0);
    init_stock(&system.stocks[2], "MSFT", 300.0);
    init_stock(&system.stocks[3], "TSLA", 250.0);
    init_stock(&system.stocks[4], "AMZN", 3300.0);
    init_stock(&system.stocks[5], "NVDA", 450.0);
    init_stock(&system.stocks[6], "META", 330.0);
    init_stock(&system.stocks[7], "NFLX", 400.0);
    init_stock(&system.stocks[8], "BABA", 80.0);
    init_stock(&system.stocks[9], "ORCL", 120.0);

    // Initialize account
    init_account(&system.account, 1, 10000.0);
    load_account_from_file(ACCOUNT_FILENAME, &system.account);

    // Initialize queue
    init_queue(&system.queue);

    // Create threads
    pthread_t price_thread, order_thread, ui_thread_id;

    pthread_create(&price_thread, NULL, price_updater, &system);
    pthread_create(&order_thread, NULL, order_processor, &system);

    UIArgs ui_args = {
        .queue = &system.queue,
        .account = &system.account,
        .stocks = system.stocks,
        .num_stocks = NUM_STOCKS,
        .selected_stock = 0
    };
    pthread_create(&ui_thread_id, NULL, ui_thread, &ui_args);

    // Wait for UI to finish
    pthread_join(ui_thread_id, NULL);

    // Stop other threads
    running = 0;

    // Wait for threads to finish
    pthread_join(price_thread, NULL);
    pthread_join(order_thread, NULL);

    save_account_to_file(ACCOUNT_FILENAME, &system.account);
    return 0;
}