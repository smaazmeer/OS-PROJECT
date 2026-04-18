#ifndef ORDER_H
#define ORDER_H

typedef enum { BUY, SELL } OrderType;

typedef struct {
    OrderType type;
    char stock_name[50];
    int quantity;
    double price; // for limit orders, but for simplicity, market orders
    int account_id;
} Order;

#endif