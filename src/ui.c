#include "ui.h"
#include "order_queue.h"
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <string.h>
#include <stdlib.h>

static double get_price_change_percent(Stock *stock) {
    double current = get_price(stock);
    int prev_index = stock->history_index == 0 ? HISTORY_SIZE - 1 : stock->history_index - 1;
    double previous = stock->history[prev_index];
    if (previous == 0.0) {
        return 0.0;
    }
    return ((current - previous) / previous) * 100.0;
}

static void update_watchlist_selection(UIArgs *args);

gboolean update_display(gpointer data) {
    UIArgs *args = (UIArgs *)data;
    static int update_counter = 0;  // Used for throttling expensive operations

    if (args->watchlist_store) {
        GtkTreeIter iter;
        gboolean has_iter = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(args->watchlist_store), &iter);
        
        // If store is empty, populate it once
        if (!has_iter) {
            for (int i = 0; i < args->num_stocks; i++) {
                gtk_list_store_append(args->watchlist_store, &iter);
                char price_buf[32];
                sprintf(price_buf, "%.2f", get_price(&args->stocks[i]));
                gtk_list_store_set(args->watchlist_store, &iter,
                                   0, args->stocks[i].name,
                                   1, price_buf,
                                   2, get_price_change_percent(&args->stocks[i]),
                                   -1);
            }
        } else {
            // Update existing rows in place - more efficient than clearing and rebuilding
            int i = 0;
            do {
                if (i < args->num_stocks) {
                    char price_buf[32];
                    sprintf(price_buf, "%.2f", get_price(&args->stocks[i]));
                    gtk_list_store_set(args->watchlist_store, &iter,
                                       1, price_buf,
                                       2, get_price_change_percent(&args->stocks[i]),
                                       -1);
                }
                i++;
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(args->watchlist_store), &iter));
        }
    }

    // Update account summary labels
    char bal_buf[50];
    sprintf(bal_buf, "$%.2f", get_balance(args->account));
    gtk_label_set_text(GTK_LABEL(args->balance_label), bal_buf);

    double port_value = get_portfolio_value(args->account, args->stocks, args->num_stocks);
    char port_buf[50];
    sprintf(port_buf, "$%.2f", port_value);
    gtk_label_set_text(GTK_LABEL(args->portfolio_label), port_buf);

    double total_pl = get_total_pl(args->account, args->stocks, args->num_stocks);
    char pl_buf[50];
    sprintf(pl_buf, "$%.2f", total_pl);
    gtk_label_set_text(GTK_LABEL(args->pl_label), pl_buf);

    // Update holdings table
    gtk_list_store_clear(args->store);
    GtkTreeIter iter;
    for (int i = 0; i < args->account->num_holdings; i++) {
        gtk_list_store_append(args->store, &iter);
        const char *name = args->account->holdings[i].stock_name;
        int qty = args->account->holdings[i].quantity;
        double avg_cost = args->account->holdings[i].avg_cost;
        double current_price = 0.0;
        for (int j = 0; j < args->num_stocks; j++) {
            if (strcmp(args->stocks[j].name, name) == 0) {
                current_price = get_price(&args->stocks[j]);
                break;
            }
        }
        double pl = (current_price - avg_cost) * qty;
        gtk_list_store_set(args->store, &iter,
                           0, name,
                           1, qty,
                           2, avg_cost,
                           3, current_price,
                           4, pl,
                           -1);
    }

    // Update pending orders view
    if (args->orders_store != NULL) {
        gtk_list_store_clear(args->orders_store);
        Order snapshot[QUEUE_SIZE];
        int count = queue_snapshot(args->queue, snapshot, QUEUE_SIZE);
        for (int i = 0; i < count; i++) {
            gtk_list_store_append(args->orders_store, &iter);
            gtk_list_store_set(args->orders_store, &iter,
                               0, snapshot[i].type == BUY ? "BUY" : "SELL",
                               1, snapshot[i].stock_name,
                               2, snapshot[i].quantity,
                               3, snapshot[i].price,
                               4, snapshot[i].account_id,
                               -1);
        }
    }

    // Throttle chart redraw to every 1 second (every 2nd update cycle at 500ms)
    // This prevents Cairo rendering overhead while keeping UI responsive
    if (update_counter % 2 == 0) {
        gtk_widget_queue_draw(args->drawing_area);
    }
    update_counter++;

    // Update detail label only (without resetting selection)
    if (args->market_detail_label && args->selected_stock >= 0 && args->selected_stock < args->num_stocks) {
        char detail[200];
        sprintf(detail,
                "Symbol: %s\nPrice: %.2f\nChange: %.2f%%\n\nUse Up/Down to move selection.",
                args->stocks[args->selected_stock].name,
                get_price(&args->stocks[args->selected_stock]),
                get_price_change_percent(&args->stocks[args->selected_stock]));
        gtk_label_set_text(GTK_LABEL(args->market_detail_label), detail);
    }

    // Update Trade tab with selected stock info
    if (args->selected_stock >= 0 && args->selected_stock < args->num_stocks) {
        Stock *stock = &args->stocks[args->selected_stock];
        gtk_label_set_text(GTK_LABEL(args->trade_stock_name_label), stock->name);
        
        char price_buf[50];
        sprintf(price_buf, "Price: $%.2f", get_price(stock));
        gtk_label_set_text(GTK_LABEL(args->trade_price_label), price_buf);
        
        double change = get_price_change_percent(stock);
        char change_buf[50];
        sprintf(change_buf, "Change: %.2f%%", change);
        gtk_label_set_text(GTK_LABEL(args->trade_change_label), change_buf);
    }

    // Update order book with recent BUY and SELL orders
    if (args->trade_buy_store && args->trade_sell_store) {
        gtk_list_store_clear(args->trade_buy_store);
        gtk_list_store_clear(args->trade_sell_store);
        
        Order snapshot[QUEUE_SIZE];
        int count = queue_snapshot(args->queue, snapshot, QUEUE_SIZE);
        
        // Populate order book with recent orders (show last 10 of each type)
        int buy_count = 0, sell_count = 0;
        for (int i = count - 1; i >= 0 && (buy_count < 10 || sell_count < 10); i--) {
            GtkTreeIter tree_iter;
            char qty_buf[20];
            char price_buf[20];
            sprintf(qty_buf, "%d", snapshot[i].quantity);
            sprintf(price_buf, "%.2f", snapshot[i].price);
            
            if (snapshot[i].type == BUY && buy_count < 10) {
                gtk_list_store_append(args->trade_buy_store, &tree_iter);
                gtk_list_store_set(args->trade_buy_store, &tree_iter,
                                   0, qty_buf,
                                   1, price_buf,
                                   -1);
                buy_count++;
            } else if (snapshot[i].type == SELL && sell_count < 10) {
                gtk_list_store_append(args->trade_sell_store, &tree_iter);
                gtk_list_store_set(args->trade_sell_store, &tree_iter,
                                   0, qty_buf,
                                   1, price_buf,
                                   -1);
                sell_count++;
            }
        }
    }

    return TRUE;
}

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    UIArgs *args = (UIArgs *)data;
    guint width, height;
    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);

    // Clear background
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    cairo_paint(cr);

    // Margins
    double margin_left = 60;
    double margin_right = 30;
    double margin_top = 30;
    double margin_bottom = 50;
    double chart_width = width - margin_left - margin_right;
    double chart_height = height - margin_top - margin_bottom;

    // Get selected stock data
    if (args->selected_stock < 0 || args->selected_stock >= args->num_stocks) {
        return FALSE;
    }
    Stock *selected_stock = &args->stocks[args->selected_stock];

    // Find min and max prices for the selected stock
    double min_price = 1e9, max_price = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        double p = selected_stock->history[i];
        if (p < min_price) min_price = p;
        if (p > max_price) max_price = p;
    }
    if (max_price == min_price) max_price = min_price + 1;
    
    double price_range = max_price - min_price;
    double x_step = chart_width / (HISTORY_SIZE - 1);
    double y_scale = chart_height / price_range;

    // Draw grid lines and labels
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 0.5);

    // Horizontal grid lines and price labels
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, pango_font_description_from_string("Sans 10"));
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);

    for (int i = 0; i <= 5; i++) {
        double price = min_price + (price_range * i / 5);
        double y = margin_top + chart_height - (price - min_price) * y_scale;
        
        // Draw grid line
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_move_to(cr, margin_left, y);
        cairo_line_to(cr, width - margin_right, y);
        cairo_stroke(cr);
        
        // Draw price label
        char price_buf[20];
        sprintf(price_buf, "$%.0f", price);
        cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
        cairo_move_to(cr, 5, y + 4);
        pango_layout_set_text(layout, price_buf, -1);
        pango_cairo_show_layout(cr, layout);
    }

    // Draw axes
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_set_line_width(cr, 1.5);
    // Y-axis
    cairo_move_to(cr, margin_left, margin_top);
    cairo_line_to(cr, margin_left, height - margin_bottom);
    // X-axis
    cairo_move_to(cr, margin_left, height - margin_bottom);
    cairo_line_to(cr, width - margin_right, height - margin_bottom);
    cairo_stroke(cr);

    // Draw price chart as candlestick-like bars
    cairo_set_source_rgb(cr, 0, 1, 0);  // Green for price line
    cairo_set_line_width(cr, 2);
    
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
        double x1 = margin_left + i * x_step;
        double y1 = margin_top + chart_height - (selected_stock->history[i] - min_price) * y_scale;
        double x2 = margin_left + (i + 1) * x_step;
        double y2 = margin_top + chart_height - (selected_stock->history[(i + 1) % HISTORY_SIZE] - min_price) * y_scale;
        
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
    }
    cairo_stroke(cr);

    // Draw time labels on X-axis
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    for (int i = 0; i < HISTORY_SIZE; i += HISTORY_SIZE / 8) {
        double x = margin_left + i * x_step;
        
        // Calculate time label (simulated)
        int hour = 12 + (i / (HISTORY_SIZE / 4));
        int minute = (i % (HISTORY_SIZE / 4)) * 60 / (HISTORY_SIZE / 4);
        
        char time_buf[20];
        sprintf(time_buf, "%02d:%02d", hour % 24, minute);
        
        cairo_move_to(cr, x - 15, height - margin_bottom + 20);
        pango_layout_set_text(layout, time_buf, -1);
        pango_cairo_show_layout(cr, layout);
        
        // Draw tick mark
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_move_to(cr, x, height - margin_bottom);
        cairo_line_to(cr, x, height - margin_bottom + 5);
        cairo_stroke(cr);
    }

    // Draw title with stock name and current price
    char title_buf[100];
    sprintf(title_buf, "%s - $%.2f (%.2f%%)", 
            selected_stock->name, 
            get_price(selected_stock),
            get_price_change_percent(selected_stock));
    
    cairo_set_source_rgb(cr, 1, 1, 1);
    PangoFontDescription *title_font = pango_font_description_from_string("Sans Bold 14");
    pango_layout_set_font_description(layout, title_font);
    cairo_move_to(cr, margin_left, 5);
    pango_layout_set_text(layout, title_buf, -1);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(title_font);

    g_object_unref(layout);

    return FALSE;
}

static void create_market_tab(UIArgs *args, GtkWidget *notebook);
static void create_trade_tab(UIArgs *args, GtkWidget *notebook);
static void create_charts_tab(UIArgs *args, GtkWidget *notebook);
static void create_portfolio_tab(UIArgs *args, GtkWidget *notebook);
static void create_orders_tab(UIArgs *args, GtkWidget *notebook);
static void update_watchlist_selection(UIArgs *args);
static void watchlist_change_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static void on_watchlist_selection_changed(GtkTreeSelection *selection, gpointer user_data);
static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);

void on_buy_clicked(GtkWidget *widget, gpointer data);
void on_sell_clicked(GtkWidget *widget, gpointer data);

static void create_market_tab(UIArgs *args, GtkWidget *notebook) {
    GtkWidget *market_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(market_vbox), 10);

    GtkWidget *market_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(market_vbox), market_hbox, TRUE, TRUE, 0);

    // Watchlist panel on the left
    GtkWidget *watchlist_frame = gtk_frame_new("WATCHLIST");
    gtk_widget_set_size_request(watchlist_frame, 320, -1);
    gtk_box_pack_start(GTK_BOX(market_hbox), watchlist_frame, FALSE, FALSE, 0);

    GtkWidget *watchlist_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(watchlist_frame), watchlist_vbox);

    args->watchlist_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE);
    args->watchlist_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(args->watchlist_store));
    g_object_unref(args->watchlist_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column;

    column = gtk_tree_view_column_new_with_attributes("Symbol", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->watchlist_treeview), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Price", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->watchlist_treeview), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Change", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, watchlist_change_cell_data_func, NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->watchlist_treeview), column);

    GtkWidget *watchlist_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(watchlist_scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(watchlist_scrolled), args->watchlist_treeview);
    gtk_box_pack_start(GTK_BOX(watchlist_vbox), watchlist_scrolled, TRUE, TRUE, 0);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(args->watchlist_treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_watchlist_selection_changed), args);

    // Detail panel on the right
    GtkWidget *detail_frame = gtk_frame_new("Selected Stock");
    gtk_box_pack_start(GTK_BOX(market_hbox), detail_frame, TRUE, TRUE, 0);
    GtkWidget *detail_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(detail_frame), detail_vbox);
    args->market_detail_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(args->market_detail_label), 0.0);
    gtk_box_pack_start(GTK_BOX(detail_vbox), args->market_detail_label, FALSE, FALSE, 0);

    GtkWidget *market_label = gtk_label_new("Market");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), market_vbox, market_label);
}

static void on_trade_buy_clicked(GtkWidget *widget, gpointer data) {
    UIArgs *args = (UIArgs *)data;
    Stock *stock = &args->stocks[args->selected_stock];
    const char *qty_text = gtk_entry_get_text(GTK_ENTRY(args->qty_entry));
    const char *price_text = gtk_entry_get_text(GTK_ENTRY(args->trade_price_entry));
    
    int quantity = atoi(qty_text);
    double price = atof(price_text);
    
    if (quantity <= 0) {
        return;
    }
    
    Order order = {BUY, "", quantity, price, args->account->id};
    strcpy(order.stock_name, stock->name);
    enqueue(args->queue, order);
    
    gtk_entry_set_text(GTK_ENTRY(args->qty_entry), "");
    gtk_entry_set_text(GTK_ENTRY(args->trade_price_entry), "");
}

static void on_trade_sell_clicked(GtkWidget *widget, gpointer data) {
    UIArgs *args = (UIArgs *)data;
    Stock *stock = &args->stocks[args->selected_stock];
    const char *qty_text = gtk_entry_get_text(GTK_ENTRY(args->qty_entry));
    const char *price_text = gtk_entry_get_text(GTK_ENTRY(args->trade_price_entry));
    
    int quantity = atoi(qty_text);
    double price = atof(price_text);
    
    if (quantity <= 0) {
        return;
    }
    
    Order order = {SELL, "", quantity, price, args->account->id};
    strcpy(order.stock_name, stock->name);
    enqueue(args->queue, order);
    
    gtk_entry_set_text(GTK_ENTRY(args->qty_entry), "");
    gtk_entry_set_text(GTK_ENTRY(args->trade_price_entry), "");
}

static void create_trade_tab(UIArgs *args, GtkWidget *notebook) {
    GtkWidget *trade_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(trade_vbox), 10);

    // Top section: selected stock info (left) + order book (right)
    GtkWidget *top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(trade_vbox), top_hbox, TRUE, TRUE, 0);

    // LEFT: Selected Stock Info
    GtkWidget *stock_info_frame = gtk_frame_new("SELECTED STOCK");
    gtk_widget_set_size_request(stock_info_frame, 280, -1);
    gtk_box_pack_start(GTK_BOX(top_hbox), stock_info_frame, FALSE, FALSE, 0);
    
    GtkWidget *stock_info_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(stock_info_vbox), 10);
    gtk_container_add(GTK_CONTAINER(stock_info_frame), stock_info_vbox);
    
    args->trade_stock_name_label = gtk_label_new("--");
    PangoAttrList *attrs = pango_attr_list_new();
    PangoAttribute *size_attr = pango_attr_size_new(18 * 1024);
    pango_attr_list_insert(attrs, size_attr);
    gtk_label_set_attributes(GTK_LABEL(args->trade_stock_name_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(stock_info_vbox), args->trade_stock_name_label, FALSE, FALSE, 0);
    
    args->trade_price_label = gtk_label_new("Price: --");
    gtk_box_pack_start(GTK_BOX(stock_info_vbox), args->trade_price_label, FALSE, FALSE, 0);
    
    args->trade_change_label = gtk_label_new("Change: --");
    gtk_box_pack_start(GTK_BOX(stock_info_vbox), args->trade_change_label, FALSE, FALSE, 0);
    
    // Add spacer
    gtk_box_pack_start(GTK_BOX(stock_info_vbox), gtk_label_new(""), TRUE, TRUE, 0);

    // RIGHT: Order Book (BUY and SELL lists)
    GtkWidget *orderbook_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(top_hbox), orderbook_vbox, TRUE, TRUE, 0);

    // BUY Orders
    GtkWidget *buy_frame = gtk_frame_new("BUY ORDERS");
    gtk_box_pack_start(GTK_BOX(orderbook_vbox), buy_frame, TRUE, TRUE, 0);
    
    args->trade_buy_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *buy_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(args->trade_buy_store));
    
    GtkCellRenderer *buy_cell = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *buy_qty_col = gtk_tree_view_column_new_with_attributes("Qty", buy_cell, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(buy_treeview), buy_qty_col);
    
    GtkCellRenderer *buy_price_cell = gtk_cell_renderer_text_new();
    g_object_set(buy_price_cell, "foreground", "green", NULL);
    GtkTreeViewColumn *buy_price_col = gtk_tree_view_column_new_with_attributes("Price", buy_price_cell, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(buy_treeview), buy_price_col);
    
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(buy_treeview), TRUE);
    gtk_container_add(GTK_CONTAINER(buy_frame), buy_treeview);
    
    // SELL Orders
    GtkWidget *sell_frame = gtk_frame_new("SELL ORDERS");
    gtk_box_pack_start(GTK_BOX(orderbook_vbox), sell_frame, TRUE, TRUE, 0);
    
    args->trade_sell_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *sell_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(args->trade_sell_store));
    
    GtkCellRenderer *sell_cell = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *sell_qty_col = gtk_tree_view_column_new_with_attributes("Qty", sell_cell, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(sell_treeview), sell_qty_col);
    
    GtkCellRenderer *sell_price_cell = gtk_cell_renderer_text_new();
    g_object_set(sell_price_cell, "foreground", "red", NULL);
    GtkTreeViewColumn *sell_price_col = gtk_tree_view_column_new_with_attributes("Price", sell_price_cell, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(sell_treeview), sell_price_col);
    
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(sell_treeview), TRUE);
    gtk_container_add(GTK_CONTAINER(sell_frame), sell_treeview);

    // BOTTOM: Input fields and buttons
    GtkWidget *input_frame = gtk_frame_new("PLACE ORDER");
    gtk_box_pack_start(GTK_BOX(trade_vbox), input_frame, FALSE, FALSE, 0);
    
    GtkWidget *input_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(input_hbox), 10);
    gtk_container_add(GTK_CONTAINER(input_frame), input_hbox);
    
    // Quantity
    GtkWidget *qty_label = gtk_label_new("Qty:");
    gtk_box_pack_start(GTK_BOX(input_hbox), qty_label, FALSE, FALSE, 0);
    args->qty_entry = gtk_entry_new();
    gtk_widget_set_size_request(args->qty_entry, 80, -1);
    gtk_box_pack_start(GTK_BOX(input_hbox), args->qty_entry, FALSE, FALSE, 0);
    
    // Price
    GtkWidget *price_label = gtk_label_new("Price:");
    gtk_box_pack_start(GTK_BOX(input_hbox), price_label, FALSE, FALSE, 0);
    args->trade_price_entry = gtk_entry_new();
    gtk_widget_set_size_request(args->trade_price_entry, 80, -1);
    gtk_box_pack_start(GTK_BOX(input_hbox), args->trade_price_entry, FALSE, FALSE, 0);
    
    // Spacer
    gtk_box_pack_start(GTK_BOX(input_hbox), gtk_label_new(""), TRUE, TRUE, 0);
    
    // Buy Button (Green)
    GtkWidget *buy_button = gtk_button_new_with_label("[B] Buy");
    GtkCssProvider *buy_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(buy_css, "button { background-color: #4CAF50; color: white; font-weight: bold; }", -1, NULL);
    GtkStyleContext *buy_context = gtk_widget_get_style_context(buy_button);
    gtk_style_context_add_provider(buy_context, GTK_STYLE_PROVIDER(buy_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_signal_connect(buy_button, "clicked", G_CALLBACK(on_trade_buy_clicked), args);
    gtk_box_pack_start(GTK_BOX(input_hbox), buy_button, FALSE, FALSE, 0);
    
    // Sell Button (Red)
    GtkWidget *sell_button = gtk_button_new_with_label("[S] Sell");
    GtkCssProvider *sell_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(sell_css, "button { background-color: #f44336; color: white; font-weight: bold; }", -1, NULL);
    GtkStyleContext *sell_context = gtk_widget_get_style_context(sell_button);
    gtk_style_context_add_provider(sell_context, GTK_STYLE_PROVIDER(sell_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_signal_connect(sell_button, "clicked", G_CALLBACK(on_trade_sell_clicked), args);
    gtk_box_pack_start(GTK_BOX(input_hbox), sell_button, FALSE, FALSE, 0);

    GtkWidget *trade_label_tab = gtk_label_new("Trade");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), trade_vbox, trade_label_tab);
}

static void create_charts_tab(UIArgs *args, GtkWidget *notebook) {
    GtkWidget *charts_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(charts_vbox), 10);

    args->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(args->drawing_area, 800, 400);
    g_signal_connect(G_OBJECT(args->drawing_area), "draw", G_CALLBACK(on_draw), args);
    gtk_box_pack_start(GTK_BOX(charts_vbox), args->drawing_area, TRUE, TRUE, 0);

    GtkWidget *charts_label_tab = gtk_label_new("Charts");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), charts_vbox, charts_label_tab);
}

static void portfolio_pl_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    double pl;
    gtk_tree_model_get(model, iter, 4, &pl, -1);
    
    char pl_buf[32];
    sprintf(pl_buf, "$%.2f", pl);
    g_object_set(renderer, "text", pl_buf, NULL);
    
    if (pl >= 0) {
        g_object_set(renderer, "foreground", "#00AA00", NULL);  // Green
    } else {
        g_object_set(renderer, "foreground", "#FF4444", NULL);  // Red
    }
}

static void portfolio_value_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    double value;
    gtk_tree_model_get(model, iter, 3, &value, -1);
    
    char buf[32];
    sprintf(buf, "$%.2f", value);
    g_object_set(renderer, "text", buf, NULL);
    g_object_set(renderer, "foreground", "#FFFFFF", NULL);
}

static void portfolio_cost_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    double cost;
    gtk_tree_model_get(model, iter, 2, &cost, -1);
    
    char buf[32];
    sprintf(buf, "$%.2f", cost);
    g_object_set(renderer, "text", buf, NULL);
    g_object_set(renderer, "foreground", "#FFFFFF", NULL);
}

static void create_portfolio_tab(UIArgs *args, GtkWidget *notebook) {
    GtkWidget *portfolio_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(portfolio_vbox), 15);

    // Account Summary Section
    GtkWidget *summary_frame = gtk_frame_new("ACCOUNT SUMMARY");
    gtk_box_pack_start(GTK_BOX(portfolio_vbox), summary_frame, FALSE, FALSE, 0);
    GtkWidget *summary_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(summary_vbox), 15);
    gtk_container_add(GTK_CONTAINER(summary_frame), summary_vbox);

    // Top row: Balance, Portfolio Value in grid
    GtkWidget *summary_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(summary_grid), 30);
    gtk_grid_set_row_spacing(GTK_GRID(summary_grid), 8);
    gtk_box_pack_start(GTK_BOX(summary_vbox), summary_grid, FALSE, FALSE, 0);

    // Cash Balance
    GtkWidget *balance_title = gtk_label_new("Cash Balance:");
    gtk_widget_set_halign(balance_title, GTK_ALIGN_END);
    GtkCssProvider *title_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(title_css, "label { font-weight: bold; color: #CCCCCC; }", -1, NULL);
    GtkStyleContext *title_ctx = gtk_widget_get_style_context(balance_title);
    gtk_style_context_add_provider(title_ctx, GTK_STYLE_PROVIDER(title_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_grid_attach(GTK_GRID(summary_grid), balance_title, 0, 0, 1, 1);

    args->balance_label = gtk_label_new("$0.00");
    gtk_widget_set_halign(args->balance_label, GTK_ALIGN_START);
    GtkCssProvider *balance_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(balance_css, "label { font-size: 16px; color: #44DD44; font-weight: bold; }", -1, NULL);
    GtkStyleContext *balance_ctx = gtk_widget_get_style_context(args->balance_label);
    gtk_style_context_add_provider(balance_ctx, GTK_STYLE_PROVIDER(balance_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_grid_attach(GTK_GRID(summary_grid), args->balance_label, 1, 0, 1, 1);

    // Portfolio Value
    GtkWidget *portfolio_title = gtk_label_new("Portfolio Value:");
    gtk_widget_set_halign(portfolio_title, GTK_ALIGN_END);
    GtkStyleContext *ptitle_ctx = gtk_widget_get_style_context(portfolio_title);
    gtk_style_context_add_provider(ptitle_ctx, GTK_STYLE_PROVIDER(title_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_grid_attach(GTK_GRID(summary_grid), portfolio_title, 0, 1, 1, 1);

    args->portfolio_label = gtk_label_new("$0.00");
    gtk_widget_set_halign(args->portfolio_label, GTK_ALIGN_START);
    GtkCssProvider *portfolio_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(portfolio_css, "label { font-size: 16px; color: #4488FF; font-weight: bold; }", -1, NULL);
    GtkStyleContext *portfolio_ctx = gtk_widget_get_style_context(args->portfolio_label);
    gtk_style_context_add_provider(portfolio_ctx, GTK_STYLE_PROVIDER(portfolio_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_grid_attach(GTK_GRID(summary_grid), args->portfolio_label, 1, 1, 1, 1);

    // Total P/L
    GtkWidget *pl_title = gtk_label_new("Total P/L:");
    gtk_widget_set_halign(pl_title, GTK_ALIGN_END);
    GtkStyleContext *pltitle_ctx = gtk_widget_get_style_context(pl_title);
    gtk_style_context_add_provider(pltitle_ctx, GTK_STYLE_PROVIDER(title_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_grid_attach(GTK_GRID(summary_grid), pl_title, 0, 2, 1, 1);

    args->pl_label = gtk_label_new("$0.00");
    gtk_widget_set_halign(args->pl_label, GTK_ALIGN_START);
    GtkCssProvider *pl_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(pl_css, "label { font-size: 16px; color: #FFAA00; font-weight: bold; }", -1, NULL);
    GtkStyleContext *pl_ctx = gtk_widget_get_style_context(args->pl_label);
    gtk_style_context_add_provider(pl_ctx, GTK_STYLE_PROVIDER(pl_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_grid_attach(GTK_GRID(summary_grid), args->pl_label, 1, 2, 1, 1);

    // Holdings Table Section
    GtkWidget *holdings_frame = gtk_frame_new("HOLDINGS");
    gtk_box_pack_start(GTK_BOX(portfolio_vbox), holdings_frame, TRUE, TRUE, 0);

    args->store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
    args->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(args->store));
    g_object_unref(args->store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkCellRenderer *numeric_renderer = gtk_cell_renderer_text_new();
    g_object_set(numeric_renderer, "xalign", 0.5, NULL);  // Center align
    
    GtkTreeViewColumn *column;

    // Symbol column
    column = gtk_tree_view_column_new_with_attributes("SYMBOL", renderer, "text", 0, NULL);
    gtk_tree_view_column_set_min_width(column, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->treeview), column);

    // Quantity column
    column = gtk_tree_view_column_new_with_attributes("QUANTITY", numeric_renderer, "text", 1, NULL);
    gtk_tree_view_column_set_min_width(column, 100);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->treeview), column);

    // Average Cost column
    renderer = gtk_cell_renderer_text_new();
    g_object_set(numeric_renderer, "xalign", 0.5, NULL);
    column = gtk_tree_view_column_new_with_attributes("AVG COST", numeric_renderer, "text", 2, NULL);
    gtk_tree_view_column_set_min_width(column, 110);
    gtk_tree_view_column_set_cell_data_func(column, numeric_renderer, portfolio_cost_cell_data_func, NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->treeview), column);

    // Current Price column
    renderer = gtk_cell_renderer_text_new();
    g_object_set(numeric_renderer, "xalign", 0.5, NULL);
    column = gtk_tree_view_column_new_with_attributes("CURRENT", numeric_renderer, "text", 3, NULL);
    gtk_tree_view_column_set_min_width(column, 110);
    gtk_tree_view_column_set_cell_data_func(column, numeric_renderer, portfolio_value_cell_data_func, NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->treeview), column);

    // P/L column
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.5, NULL);
    column = gtk_tree_view_column_new_with_attributes("PROFIT/LOSS", renderer, "text", 4, NULL);
    gtk_tree_view_column_set_min_width(column, 120);
    gtk_tree_view_column_set_cell_data_func(column, renderer, portfolio_pl_cell_data_func, NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->treeview), column);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(args->treeview), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(args->treeview), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), args->treeview);
    gtk_container_add(GTK_CONTAINER(holdings_frame), scrolled_window);

    GtkWidget *portfolio_label_tab = gtk_label_new("Portfolio");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), portfolio_vbox, portfolio_label_tab);
}

static void order_type_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    char *type_str;
    gtk_tree_model_get(model, iter, 0, &type_str, -1);
    
    if (type_str) {
        if (strcmp(type_str, "BUY") == 0) {
            g_object_set(renderer, "foreground", "#00DD00", NULL);  // Green
            g_object_set(renderer, "text", "BUY", NULL);
        } else if (strcmp(type_str, "SELL") == 0) {
            g_object_set(renderer, "foreground", "#FF4444", NULL);  // Red
            g_object_set(renderer, "text", "SELL", NULL);
        }
        g_free(type_str);
    }
}

static void order_price_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    double price;
    gtk_tree_model_get(model, iter, 3, &price, -1);
    
    char buf[32];
    sprintf(buf, "$%.2f", price);
    g_object_set(renderer, "text", buf, NULL);
}

static void create_orders_tab(UIArgs *args, GtkWidget *notebook) {
    GtkWidget *orders_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(orders_vbox), 15);

    // PENDING ORDERS Section
    GtkWidget *pending_frame = gtk_frame_new("PENDING ORDERS");
    gtk_box_pack_start(GTK_BOX(orders_vbox), pending_frame, FALSE, TRUE, 0);

    args->orders_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_INT);
    args->orders_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(args->orders_store));
    g_object_unref(args->orders_store);

    GtkCellRenderer *order_renderer = gtk_cell_renderer_text_new();
    GtkCellRenderer *type_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *order_column;

    // Type column with color coding
    order_column = gtk_tree_view_column_new_with_attributes("TYPE", type_renderer, "text", 0, NULL);
    gtk_tree_view_column_set_cell_data_func(order_column, type_renderer, order_type_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_min_width(order_column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->orders_treeview), order_column);

    // Stock column
    order_column = gtk_tree_view_column_new_with_attributes("STOCK", order_renderer, "text", 1, NULL);
    gtk_tree_view_column_set_min_width(order_column, 70);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->orders_treeview), order_column);

    // Quantity column
    order_renderer = gtk_cell_renderer_text_new();
    g_object_set(order_renderer, "xalign", 0.5, NULL);
    order_column = gtk_tree_view_column_new_with_attributes("QTY", order_renderer, "text", 2, NULL);
    gtk_tree_view_column_set_min_width(order_column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->orders_treeview), order_column);

    // Price column with currency formatting
    GtkCellRenderer *price_renderer = gtk_cell_renderer_text_new();
    g_object_set(price_renderer, "xalign", 0.5, NULL);
    order_column = gtk_tree_view_column_new_with_attributes("PRICE", price_renderer, "text", 3, NULL);
    gtk_tree_view_column_set_cell_data_func(order_column, price_renderer, order_price_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_min_width(order_column, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->orders_treeview), order_column);

    // Account ID column
    order_renderer = gtk_cell_renderer_text_new();
    g_object_set(order_renderer, "xalign", 0.5, NULL);
    order_column = gtk_tree_view_column_new_with_attributes("ACCT", order_renderer, "text", 4, NULL);
    gtk_tree_view_column_set_min_width(order_column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->orders_treeview), order_column);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(args->orders_treeview), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(args->orders_treeview), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

    GtkWidget *pending_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pending_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(pending_scrolled), args->orders_treeview);
    gtk_container_add(GTK_CONTAINER(pending_frame), pending_scrolled);

    // TRADE HISTORY Section
    GtkWidget *history_frame = gtk_frame_new("TRADE HISTORY");
    gtk_box_pack_start(GTK_BOX(orders_vbox), history_frame, TRUE, TRUE, 0);

    // Create trade history store: Type, Stock, Qty, Price, Time
    args->trade_history_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_STRING);
    args->trade_history_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(args->trade_history_store));
    g_object_unref(args->trade_history_store);

    GtkCellRenderer *history_type_renderer = gtk_cell_renderer_text_new();
    GtkCellRenderer *history_renderer = gtk_cell_renderer_text_new();

    // Type column
    GtkTreeViewColumn *history_column = gtk_tree_view_column_new_with_attributes("TYPE", history_type_renderer, "text", 0, NULL);
    gtk_tree_view_column_set_cell_data_func(history_column, history_type_renderer, order_type_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_min_width(history_column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->trade_history_treeview), history_column);

    // Stock column
    history_column = gtk_tree_view_column_new_with_attributes("STOCK", history_renderer, "text", 1, NULL);
    gtk_tree_view_column_set_min_width(history_column, 70);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->trade_history_treeview), history_column);

    // Quantity column
    history_renderer = gtk_cell_renderer_text_new();
    g_object_set(history_renderer, "xalign", 0.5, NULL);
    history_column = gtk_tree_view_column_new_with_attributes("QTY", history_renderer, "text", 2, NULL);
    gtk_tree_view_column_set_min_width(history_column, 60);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->trade_history_treeview), history_column);

    // Price column
    GtkCellRenderer *history_price_renderer = gtk_cell_renderer_text_new();
    g_object_set(history_price_renderer, "xalign", 0.5, NULL);
    history_column = gtk_tree_view_column_new_with_attributes("PRICE", history_price_renderer, "text", 3, NULL);
    gtk_tree_view_column_set_cell_data_func(history_column, history_price_renderer, order_price_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_min_width(history_column, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->trade_history_treeview), history_column);

    // Time column
    history_renderer = gtk_cell_renderer_text_new();
    g_object_set(history_renderer, "xalign", 0.5, NULL);
    history_column = gtk_tree_view_column_new_with_attributes("TIME", history_renderer, "text", 4, NULL);
    gtk_tree_view_column_set_min_width(history_column, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(args->trade_history_treeview), history_column);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(args->trade_history_treeview), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(args->trade_history_treeview), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

    GtkWidget *history_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(history_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(history_scrolled), args->trade_history_treeview);
    gtk_container_add(GTK_CONTAINER(history_frame), history_scrolled);

    // Initialize trade tracking
    args->trade_count = 0;
    args->last_trade_time = 0;

    // Sample trade history data
    GtkTreeIter iter;
    char time_buf[32];
    
    gtk_list_store_append(args->trade_history_store, &iter);
    strcpy(time_buf, "14:32:15");
    gtk_list_store_set(args->trade_history_store, &iter,
                       0, "BUY",
                       1, "AAPL",
                       2, 50,
                       3, 145.50,
                       4, time_buf,
                       -1);

    gtk_list_store_append(args->trade_history_store, &iter);
    strcpy(time_buf, "14:28:42");
    gtk_list_store_set(args->trade_history_store, &iter,
                       0, "SELL",
                       1, "GOOG",
                       2, 25,
                       3, 2795.75,
                       4, time_buf,
                       -1);

    gtk_list_store_append(args->trade_history_store, &iter);
    strcpy(time_buf, "14:15:03");
    gtk_list_store_set(args->trade_history_store, &iter,
                       0, "BUY",
                       1, "MSFT",
                       2, 75,
                       3, 298.25,
                       4, time_buf,
                       -1);

    gtk_list_store_append(args->trade_history_store, &iter);
    strcpy(time_buf, "14:08:19");
    gtk_list_store_set(args->trade_history_store, &iter,
                       0, "SELL",
                       1, "TSLA",
                       2, 40,
                       3, 248.90,
                       4, time_buf,
                       -1);

    gtk_list_store_append(args->trade_history_store, &iter);
    strcpy(time_buf, "14:02:55");
    gtk_list_store_set(args->trade_history_store, &iter,
                       0, "BUY",
                       1, "NVDA",
                       2, 30,
                       3, 445.60,
                       4, time_buf,
                       -1);

    gtk_list_store_append(args->trade_history_store, &iter);
    strcpy(time_buf, "13:55:30");
    gtk_list_store_set(args->trade_history_store, &iter,
                       0, "SELL",
                       1, "AMZN",
                       2, 20,
                       3, 3310.25,
                       4, time_buf,
                       -1);

    GtkWidget *orders_label_tab = gtk_label_new("Orders");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), orders_vbox, orders_label_tab);
}

static void update_watchlist_selection(UIArgs *args) {
    if (!args->watchlist_treeview) {
        return;
    }

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(args->watchlist_treeview));
    GtkTreePath *path = gtk_tree_path_new_from_indices(args->selected_stock, -1);
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_path_free(path);

    if (args->market_detail_label && args->selected_stock >= 0 && args->selected_stock < args->num_stocks) {
        char detail[200];
        sprintf(detail,
                "Symbol: %s\nPrice: %.2f\nChange: %.2f%%\n\nUse Up/Down to move selection.",
                args->stocks[args->selected_stock].name,
                get_price(&args->stocks[args->selected_stock]),
                get_price_change_percent(&args->stocks[args->selected_stock]));
        gtk_label_set_text(GTK_LABEL(args->market_detail_label), detail);
    }
}

static void watchlist_change_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    double change;
    gtk_tree_model_get(model, iter, 2, &change, -1);
    char buf[32];
    sprintf(buf, "%.2f%%", change);
    g_object_set(renderer, "text", buf, NULL);
    if (change >= 0) {
        g_object_set(renderer, "foreground", "green", NULL);
    } else {
        g_object_set(renderer, "foreground", "red", NULL);
    }
}

static void on_watchlist_selection_changed(GtkTreeSelection *selection, gpointer user_data) {
    UIArgs *args = (UIArgs *)user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        if (path) {
            int *indices = gtk_tree_path_get_indices(path);
            if (indices) {
                args->selected_stock = indices[0];
                update_watchlist_selection(args);
            }
            gtk_tree_path_free(path);
        }
    }
}

static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    UIArgs *args = (UIArgs *)data;
    if (event->keyval == GDK_KEY_Up) {
        if (args->selected_stock > 0) {
            args->selected_stock--;
            update_watchlist_selection(args);
        }
    } else if (event->keyval == GDK_KEY_Down) {
        if (args->selected_stock < args->num_stocks - 1) {
            args->selected_stock++;
            update_watchlist_selection(args);
        }
    }
    return FALSE;
}

void on_buy_clicked(GtkWidget *widget, gpointer data) {
    UIArgs *args = (UIArgs *)data;
    const char *stock_name = gtk_entry_get_text(GTK_ENTRY(args->stock_entry));
    const char *qty_text = gtk_entry_get_text(GTK_ENTRY(args->qty_entry));
    int quantity = atoi(qty_text);

    Order order = {BUY, "", quantity, 0.0, args->account->id};
    strcpy(order.stock_name, stock_name);
    enqueue(args->queue, order);
}

void on_sell_clicked(GtkWidget *widget, gpointer data) {
    UIArgs *args = (UIArgs *)data;
    const char *stock_name = gtk_entry_get_text(GTK_ENTRY(args->stock_entry));
    const char *qty_text = gtk_entry_get_text(GTK_ENTRY(args->qty_entry));
    int quantity = atoi(qty_text);

    Order order = {SELL, "", quantity, 0.0, args->account->id};
    strcpy(order.stock_name, stock_name);
    enqueue(args->queue, order);
}

void *ui_thread(void *arg) {
    UIArgs *args = (UIArgs *)arg;

    gtk_init(NULL, NULL);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Stock Trading System");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    args->notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), args->notebook);

    create_market_tab(args, args->notebook);
    create_trade_tab(args, args->notebook);
    create_charts_tab(args, args->notebook);
    create_portfolio_tab(args, args->notebook);
    create_orders_tab(args, args->notebook);

    g_signal_connect(window, "key-press-event", G_CALLBACK(on_window_key_press), args);
    gtk_widget_show_all(window);

    // Update display every 500ms for smooth real-time updates
    // GTK handles double-buffering internally to prevent flickering
    g_timeout_add(500, update_display, args);

    gtk_main();

    return NULL;
}