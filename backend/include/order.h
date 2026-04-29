#ifndef ORDER_H
#define ORDER_H

typedef enum { BUY, SELL } OrderType;
typedef enum { MARKET, LIMIT, OCO } OrderKind;
typedef enum { PENDING, FILLED, CANCELLED } OrderStatus; // NEW: Track status

typedef struct {
    OrderType type;
    OrderKind kind;
    char stock_name[50];
    int quantity;
    double price;      // limit price for LIMIT or OCO stop-limit
    double stop_price; // stop trigger for OCO orders
    double oco_price;  // take-profit price for OCO orders
    int account_id;
    int order_id;
    int oco_group_id;
    OrderStatus status; // NEW: Current state of the order
} Order;

#endif