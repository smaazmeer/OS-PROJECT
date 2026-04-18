#ifndef UI_H
#define UI_H

#include "stock.h"
#include "account.h"
#include "order_queue.h"
#include <gtk/gtk.h>

#define NUM_STOCKS 10

typedef struct {
    OrderQueue *queue;
    Account *account;
    Stock *stocks;
    int num_stocks;
    int selected_stock;
    GtkWidget *watchlist_treeview;
    GtkListStore *watchlist_store;
    GtkWidget *market_detail_label;
    GtkWidget *balance_label;
    GtkWidget *portfolio_label;
    GtkWidget *pl_label;
    GtkWidget *treeview;
    GtkListStore *store;
    GtkWidget *orders_treeview;
    GtkListStore *orders_store;
    GtkWidget *trade_history_treeview;
    GtkListStore *trade_history_store;
    GtkWidget *stock_entry;
    GtkWidget *qty_entry;
    GtkWidget *notebook;
    GtkWidget *drawing_area;
    // Trade tab widgets
    GtkWidget *trade_stock_name_label;
    GtkWidget *trade_price_label;
    GtkWidget *trade_change_label;
    GtkListStore *trade_buy_store;
    GtkListStore *trade_sell_store;
    GtkWidget *trade_price_entry;
    // Trade history tracking
    int trade_count;
    time_t last_trade_time;
} UIArgs;

void *ui_thread(void *arg);

#endif