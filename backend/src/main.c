#include "mongoose.h"
#include "trading.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

TradingSystem system_state;
int running = 1;

#define global_account      system_state.account
#define global_order_queue  system_state.queue

void* order_execution_thread(void* arg);
int   place_secure_order(Account *acc, Order *new_order);
void  process_cancel_order(Account *acc, int order_id);

// ─── /api/stocks ──────────────────────────────────────────────────────────
static void handle_get_stocks(struct mg_connection *c, int ev, void *ev_data) {
    char json[8192];
    int pos = snprintf(json, sizeof(json), "[");
    int first = 1;
    for (int i = 0; i < NUM_STOCKS; i++) {
        if (system_state.stocks[i].name[0] == '\0') continue;
        double price = get_price(&system_state.stocks[i]);
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"name\":\"%s\",\"price\":%.2f}",
            first ? "" : ",", system_state.stocks[i].name, price);
        first = 0;
    }
    snprintf(json + pos, sizeof(json) - pos, "]");
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
        "%s", json);
}

// ─── /api/account ─────────────────────────────────────────────────────────
static void handle_get_account(struct mg_connection *c, int ev, void *ev_data) {
    double balance    = get_balance(&global_account);
    double available  = get_available_balance(&global_account);
    double portfolio  = get_portfolio_value(&global_account, system_state.stocks, NUM_STOCKS);
    double unrealised = get_total_pl(&global_account, system_state.stocks, NUM_STOCKS);
    double realised   = get_realised_pl(&global_account);
    double total_pl   = unrealised + realised;
    double initial    = global_account.initial_balance;

    char json[4096];
    int pos = snprintf(json, sizeof(json),
        "{\"balance\":%.2f,\"available\":%.2f,"
        "\"portfolio_value\":%.2f,"
        "\"total_pl\":%.2f,"
        "\"unrealised_pl\":%.2f,"
        "\"realised_pl\":%.2f,"
        "\"initial_balance\":%.2f,"
        "\"holdings\":[",
        balance, available, portfolio,
        total_pl, unrealised, realised, initial);
    pthread_mutex_lock(&global_account.account_mutex);
    for (int i = 0; i < global_account.num_holdings; i++) {
        Holding *h = &global_account.holdings[i];
        double cur = h->avg_cost;
        for (int j = 0; j < NUM_STOCKS; j++) {
            if (strcmp(system_state.stocks[j].name, h->stock_name) == 0) {
                cur = get_price(&system_state.stocks[j]);
                break;
            }
        }
        double pl_holding = (cur - h->avg_cost) * h->quantity;
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"symbol\":\"%s\",\"qty\":%d,\"locked\":%d,"
            "\"avg_cost\":%.2f,\"current_price\":%.2f,\"pl\":%.2f}",
            (i > 0 ? "," : ""),
            h->stock_name, h->quantity, h->locked_quantity,
            h->avg_cost, cur, pl_holding);
    }
    pthread_mutex_unlock(&global_account.account_mutex);

    snprintf(json + pos, sizeof(json) - pos, "]}");
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
        "%s", json);
}

// ─── /api/orders (pending only) ───────────────────────────────────────────
static void handle_get_orders(struct mg_connection *c, int ev, void *ev_data) {
    Order snap[MAX_PENDING_ORDERS];
    int count = get_pending_orders(&system_state, snap, MAX_PENDING_ORDERS);

    char json[8192];
    int pos = snprintf(json, sizeof(json), "[");
    for (int i = 0; i < count; i++) {
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"order_id\":%d,\"symbol\":\"%s\","
            "\"type\":\"%s\",\"kind\":\"%s\","
            "\"qty\":%d,\"price\":%.2f,\"stop_price\":%.2f,\"status\":\"%s\"}",
            (i > 0 ? "," : ""),
            snap[i].order_id, snap[i].stock_name,
            snap[i].type == BUY ? "BUY" : "SELL",
            snap[i].kind == MARKET ? "MARKET" :
            snap[i].kind == LIMIT  ? "LIMIT"  : "OCO",
            snap[i].quantity, snap[i].price, snap[i].stop_price,
            snap[i].status == PENDING ? "PENDING" :
            snap[i].status == FILLED  ? "FILLED"  : "CANCELLED");
    }
    snprintf(json + pos, sizeof(json) - pos, "]");
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
        "%s", json);
}

// ─── /api/history (filled + cancelled) ───────────────────────────────────
static void handle_get_history(struct mg_connection *c, int ev, void *ev_data) {
    Order snap[MAX_HISTORY_ORDERS];
    int count = get_order_history(&system_state, snap, MAX_HISTORY_ORDERS);

    char json[32768]; // history can be large
    int pos = snprintf(json, sizeof(json), "[");
    for (int i = 0; i < count; i++) {
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"order_id\":%d,\"symbol\":\"%s\","
            "\"type\":\"%s\",\"kind\":\"%s\","
            "\"qty\":%d,\"price\":%.2f,\"stop_price\":%.2f,\"status\":\"%s\"}",
            (i > 0 ? "," : ""),
            snap[i].order_id, snap[i].stock_name,
            snap[i].type == BUY ? "BUY" : "SELL",
            snap[i].kind == MARKET ? "MARKET" :
            snap[i].kind == LIMIT  ? "LIMIT"  : "OCO",
            snap[i].quantity, snap[i].price, snap[i].stop_price,
            snap[i].status == PENDING   ? "PENDING"   :
            snap[i].status == FILLED    ? "FILLED"    : "CANCELLED");
    }
    snprintf(json + pos, sizeof(json) - pos, "]");
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
        "%s", json);
}

// ─── /api/order (POST) ────────────────────────────────────────────────────
static void handle_post_order(struct mg_connection *c, struct mg_http_message *hm) {
    char *p_symbol = mg_json_get_str(hm->body, "$.symbol");
    char *p_side   = mg_json_get_str(hm->body, "$.side");
    char *p_type   = mg_json_get_str(hm->body, "$.type");

    double qty = 0, limit_price = 0, stop_price = 0;
    mg_json_get_num(hm->body, "$.qty",         &qty);
    mg_json_get_num(hm->body, "$.limit_price", &limit_price);
    mg_json_get_num(hm->body, "$.stop_price",  &stop_price);

    Order new_order;
    memset(&new_order, 0, sizeof(Order));
    if (p_symbol) snprintf(new_order.stock_name, sizeof(new_order.stock_name), "%s", p_symbol);

    new_order.type  = (p_side && strcmp(p_side, "BUY") == 0) ? BUY : SELL;
    new_order.kind  = MARKET;
    if (p_type) {
        if      (strcmp(p_type, "LIMIT") == 0) new_order.kind = LIMIT;
        else if (strcmp(p_type, "OCO")   == 0) new_order.kind = OCO;
    }
    new_order.quantity   = (int)qty;
    // For OCO: price = take-profit/dip-buy, stop_price = stop-loss/breakout
    new_order.price      = limit_price;
    new_order.stop_price = stop_price;
    new_order.status     = PENDING;
    new_order.order_id   = rand() % 100000;
    new_order.account_id = 1;

    if (place_secure_order(&global_account, &new_order)) {
        mg_http_reply(c, 200,
            "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
            "{\"status\":\"success\",\"message\":\"Order placed\",\"order_id\":%d}",
            new_order.order_id);
    } else {
        mg_http_reply(c, 400,
            "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
            "{\"status\":\"error\",\"message\":\"Insufficient funds or shares\"}");
    }
    free(p_symbol); free(p_side); free(p_type);
}

// ─── /api/cancel (POST) ───────────────────────────────────────────────────
static void handle_cancel_order(struct mg_connection *c, struct mg_http_message *hm) {
    double order_id_dbl = 0;
    mg_json_get_num(hm->body, "$.order_id", &order_id_dbl);
    process_cancel_order(&global_account, (int)order_id_dbl);
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n",
        "{\"status\":\"success\",\"message\":\"Cancellation requested\"}");
}

// ─── Router ───────────────────────────────────────────────────────────────
static void api_router(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
        mg_http_reply(c, 204,
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n", "");
        return;
    }

    if      (mg_match(hm->uri, mg_str("/api/stocks"),  NULL)) handle_get_stocks(c, ev, ev_data);
    else if (mg_match(hm->uri, mg_str("/api/account"), NULL)) handle_get_account(c, ev, ev_data);
    else if (mg_match(hm->uri, mg_str("/api/orders"),  NULL)) handle_get_orders(c, ev, ev_data);
    else if (mg_match(hm->uri, mg_str("/api/history"), NULL)) handle_get_history(c, ev, ev_data);
    else if (mg_match(hm->uri, mg_str("/api/order"),   NULL)) handle_post_order(c, hm);
    else if (mg_match(hm->uri, mg_str("/api/cancel"),  NULL)) handle_cancel_order(c, hm);
    else mg_http_reply(c, 404,
            "Access-Control-Allow-Origin: *\r\n",
            "{\"error\":\"Not Found\"}");
}

// ─── main ─────────────────────────────────────────────────────────────────
int main(void) {
    srand(time(NULL));

    system_state.running = &running;
    init_account(&system_state.account, 1, 100000.0);
    init_queue(&system_state.queue);
    pthread_mutex_init(&system_state.pending_mutex, NULL);
    pthread_mutex_init(&system_state.history_mutex, NULL);
    system_state.pending_count = 0;
    system_state.history_count = 0;

    init_stock(&system_state.stocks[0], "AAPL",   178.50);
    init_stock(&system_state.stocks[1], "MSFT",   415.20);
    init_stock(&system_state.stocks[2], "GOOG",   175.80);
    init_stock(&system_state.stocks[3], "AMZN",   198.30);
    init_stock(&system_state.stocks[4], "NVDA",   875.40);
    init_stock(&system_state.stocks[5], "TSLA",   177.90);
    init_stock(&system_state.stocks[6], "META",   525.60);
    init_stock(&system_state.stocks[7], "JPM",    198.70);
    init_stock(&system_state.stocks[8], "V",      275.30);
    init_stock(&system_state.stocks[9], "NFLX",   625.10);

    pthread_t price_thread, order_thread, exec_thread;
    pthread_create(&price_thread, NULL, price_updater,          &system_state);
    pthread_create(&order_thread, NULL, order_processor,        &system_state);
    pthread_create(&exec_thread,  NULL, order_execution_thread, NULL);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:8000", api_router, NULL);
    printf("Backend API running on http://localhost:8000\n");

    while (running) mg_mgr_poll(&mgr, 100);
    mg_mgr_free(&mgr);
    return 0;
}