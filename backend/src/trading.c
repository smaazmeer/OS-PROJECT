#include "trading.h"
#include "stock.h"
#include "account.h"
#include "order_queue.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

extern TradingSystem system_state;
#define global_account     system_state.account
#define global_order_queue system_state.queue

// ─── Order ID ─────────────────────────────────────────────────────────────
static pthread_mutex_t order_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int next_order_id = 1;
static int generate_order_id(void) {
    pthread_mutex_lock(&order_id_mutex);
    int id = next_order_id++;
    pthread_mutex_unlock(&order_id_mutex);
    return id;
}

// ─── Reservation amount for a pending BUY order ──────────────────────────
static double get_order_reservation(const Order *o) {
    if (o->type == BUY) {
        if (o->kind == LIMIT) return o->price * o->quantity;
        if (o->kind == OCO)   return fmax(o->price, o->stop_price) * o->quantity;
    }
    return 0.0;
}

// ─── LIMIT trigger ────────────────────────────────────────────────────────
static bool limit_order_triggered(const Order *o, double market) {
    if (o->kind != LIMIT) return false;
    return (o->type == BUY) ? (market <= o->price) : (market >= o->price);
}

// ─── OCO trigger — BUG FIX ────────────────────────────────────────────────
//
// What OCO means in this system:
//   SELL OCO: you hold shares. Two exits are set:
//     • stop_price  — stop-loss: if price DROPS to/below this, sell to limit loss
//     • price       — take-profit: if price RISES to/above this, sell to lock gain
//   BUY OCO: two entry points:
//     • stop_price  — breakout buy: if price RISES to/above this, buy on momentum
//     • price       — dip buy: if price DROPS to/below this, buy the dip
//
// THE OLD BUG: The condition fired immediately because the current price
// was already between the two legs (e.g. stock at $150, stop=$140, tp=$160 →
// $150 <= $140 is false but $150 >= $160 is also false — BUT the old code
// for SELL was: market <= stop_price || market >= oco_price which would fire
// if either leg is touched. The real issue was oco_price was being used
// as the take-profit but the frontend was sending it in the wrong field,
// and the reservation used fmax(price, oco_price) when it should use
// fmax(price, stop_price) for a BUY OCO.
//
// CORRECT LOGIC:
//   SELL OCO fires when: market <= stop_price (stop-loss hit)
//                     OR market >= price      (take-profit hit)
//   BUY  OCO fires when: market >= stop_price (breakout hit)
//                     OR market <= price      (dip-buy hit)
//
// Fields used:
//   o->price      = take-profit price (for SELL) / dip-buy price (for BUY)
//   o->stop_price = stop-loss price   (for SELL) / breakout price (for BUY)
//
static bool oco_order_triggered(const Order *o, double market) {
    if (o->kind != OCO) return false;
    if (o->type == SELL) {
        // Fire if price dropped to stop-loss OR rose to take-profit
        return (market <= o->stop_price) || (market >= o->price);
    } else { // BUY
        // Fire if price rose to breakout level OR dropped to dip-buy level
        return (market >= o->stop_price) || (market <= o->price);
    }
}

// ─── Execution price for a triggered order ───────────────────────────────
static double calc_execution_price(const Order *o, double market) {
    if (o->kind == MARKET) return market;
    if (o->kind == LIMIT)
        return (o->type == BUY) ? fmin(market, o->price) : fmax(market, o->price);
    if (o->kind == OCO) {
        if (o->type == SELL) {
            // Which leg fired?
            if (market <= o->stop_price) return market; // stop-loss: execute at market
            return o->price;                            // take-profit: execute at limit
        } else { // BUY
            if (market >= o->stop_price) return market; // breakout: execute at market
            return o->price;                            // dip-buy: execute at limit
        }
    }
    return market;
}

// ─── History ─────────────────────────────────────────────────────────────
void add_to_history(TradingSystem *sys, Order order, OrderStatus status) {
    pthread_mutex_lock(&sys->history_mutex);
    if (sys->history_count < MAX_HISTORY_ORDERS) {
        order.status = status;
        sys->history[sys->history_count++] = order;
    }
    pthread_mutex_unlock(&sys->history_mutex);
}

int get_order_history(TradingSystem *sys, Order *snapshot, int max) {
    pthread_mutex_lock(&sys->history_mutex);
    int count = sys->history_count < max ? sys->history_count : max;
    // Return in reverse order (newest first)
    for (int i = 0; i < count; i++)
        snapshot[i] = sys->history[sys->history_count - 1 - i];
    pthread_mutex_unlock(&sys->history_mutex);
    return count;
}

// ─── Pending list helpers ─────────────────────────────────────────────────
static void add_pending_order(TradingSystem *sys, Order order) {
    pthread_mutex_lock(&sys->pending_mutex);
    if (sys->pending_count < MAX_PENDING_ORDERS)
        sys->pending_orders[sys->pending_count++] = order;
    pthread_mutex_unlock(&sys->pending_mutex);
}

static void release_order_resources(Account *acc, const Order *o) {
    if (o->kind == MARKET) return;
    if (o->type == BUY)
        release_cash(acc, get_order_reservation(o));
    else
        unlock_shares(acc, o->stock_name, o->quantity);
}

// ─── Core execution (single mutex, no nested locking) ─────────────────────
static void execute_order(Account *acc, const Order *o, double exec_price) {
    double cost = exec_price * o->quantity;
    pthread_mutex_lock(&acc->account_mutex);

    if (o->type == BUY) {
        if (o->kind == LIMIT || o->kind == OCO) {
            double reserved = get_order_reservation(o);
            acc->reserved_cash -= reserved;
            if (acc->reserved_cash < 0.0) acc->reserved_cash = 0.0;
        }
        acc->balance -= cost;

        bool found = false;
        for (int i = 0; i < acc->num_holdings; i++) {
            if (strcmp(acc->holdings[i].stock_name, o->stock_name) == 0) {
                double total = acc->holdings[i].quantity * acc->holdings[i].avg_cost
                               + o->quantity * exec_price;
                acc->holdings[i].quantity += o->quantity;
                acc->holdings[i].avg_cost  = total / acc->holdings[i].quantity;
                found = true;
                break;
            }
        }
        if (!found && acc->num_holdings < MAX_HOLDINGS) {
            strncpy(acc->holdings[acc->num_holdings].stock_name, o->stock_name, 49);
            acc->holdings[acc->num_holdings].quantity        = o->quantity;
            acc->holdings[acc->num_holdings].locked_quantity = 0;
            acc->holdings[acc->num_holdings].avg_cost        = exec_price;
            acc->num_holdings++;
        }

    } else { // SELL
        if (o->kind == LIMIT || o->kind == OCO) {
            for (int i = 0; i < acc->num_holdings; i++) {
                if (strcmp(acc->holdings[i].stock_name, o->stock_name) == 0) {
                    acc->holdings[i].locked_quantity -= o->quantity;
                    if (acc->holdings[i].locked_quantity < 0)
                        acc->holdings[i].locked_quantity = 0;
                    break;
                }
            }
        }
        acc->balance += cost;

        // Accumulate realised P/L before removing the holding.
        // (exec_price - avg_cost) * qty_sold = profit or loss on this sell.
        // Storing it here means closing a position never zeroes the P/L display.
        for (int i = 0; i < acc->num_holdings; i++) {
            if (strcmp(acc->holdings[i].stock_name, o->stock_name) == 0) {
                double avg = acc->holdings[i].avg_cost;
                acc->realised_pl += (exec_price - avg) * o->quantity;

                acc->holdings[i].quantity -= o->quantity;
                if (acc->holdings[i].quantity <= 0) {
                    for (int j = i; j < acc->num_holdings - 1; j++)
                        acc->holdings[j] = acc->holdings[j + 1];
                    acc->num_holdings--;
                }
                break;
            }
        }
    }

    pthread_mutex_unlock(&acc->account_mutex);
    save_account_to_file(ACCOUNT_FILENAME, acc);
    printf("[FILLED] %s %d x %s @ %.2f\n",
           o->type == BUY ? "BUY" : "SELL",
           o->quantity, o->stock_name, exec_price);
}

// ─── Process pending LIMIT/OCO orders each cycle ─────────────────────────
static void process_pending_orders(TradingSystem *sys) {
    pthread_mutex_lock(&sys->pending_mutex);
    for (int i = 0; i < sys->pending_count; ) {
        Order *o = &sys->pending_orders[i];

        if (o->status == CANCELLED) {
            add_to_history(sys, *o, CANCELLED);
            for (int j = i; j < sys->pending_count - 1; j++)
                sys->pending_orders[j] = sys->pending_orders[j + 1];
            sys->pending_count--;
            continue;
        }

        Stock *stock = NULL;
        for (int j = 0; j < NUM_STOCKS; j++) {
            if (sys->stocks[j].name[0] != '\0' &&
                strcmp(sys->stocks[j].name, o->stock_name) == 0) {
                stock = &sys->stocks[j];
                break;
            }
        }
        if (!stock) { i++; continue; }

        double market = get_price(stock);
        bool ready = (o->kind == LIMIT && limit_order_triggered(o, market))
                  || (o->kind == OCO   && oco_order_triggered(o, market));

        if (ready) {
            double exec = calc_execution_price(o, market);
            Order  copy = *o;
            for (int j = i; j < sys->pending_count - 1; j++)
                sys->pending_orders[j] = sys->pending_orders[j + 1];
            sys->pending_count--;
            pthread_mutex_unlock(&sys->pending_mutex);

            execute_order(&sys->account, &copy, exec);
            add_to_history(sys, copy, FILLED);

            pthread_mutex_lock(&sys->pending_mutex);
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&sys->pending_mutex);
}

// ─── Handle a dequeued order ──────────────────────────────────────────────
static void handle_new_order(TradingSystem *sys, Order order) {
    order.order_id     = generate_order_id();
    order.oco_group_id = order.order_id;

    Stock *stock = NULL;
    for (int i = 0; i < NUM_STOCKS; i++) {
        if (sys->stocks[i].name[0] != '\0' &&
            strcmp(sys->stocks[i].name, order.stock_name) == 0) {
            stock = &sys->stocks[i];
            break;
        }
    }
    if (!stock || order.quantity <= 0) return;

    double market = get_price(stock);

    if (order.kind == MARKET) {
        if (order.type == BUY) {
            double cost = market * order.quantity;
            if (get_available_balance(&sys->account) >= cost) {
                execute_order(&sys->account, &order, market);
                add_to_history(sys, order, FILLED);
            }
        } else {
            if (get_holding(&sys->account, order.stock_name) >= order.quantity) {
                execute_order(&sys->account, &order, market);
                add_to_history(sys, order, FILLED);
            }
        }
        return;
    }

    // LIMIT / OCO — resources already reserved by place_secure_order
    add_pending_order(sys, order);
}

// ─── Threads ─────────────────────────────────────────────────────────────
void *price_updater(void *arg) {
    TradingSystem *sys = (TradingSystem *)arg;
    srand(time(NULL) + (unsigned long)pthread_self());
    while (*sys->running) {
        usleep(500000);
        for (int i = 0; i < NUM_STOCKS; i++) {
            if (sys->stocks[i].name[0] == '\0') continue;
            double cur = get_price(&sys->stocks[i]);
            double pct = (rand() % 100 - 50) / 10000.0;
            double nxt = cur * (1.0 + pct);
            if (nxt < 1.0)       nxt = 1.0;
            if (nxt > cur * 2.0) nxt = cur * 1.5;
            update_price(&sys->stocks[i], nxt);
        }
    }
    return NULL;
}

void *order_processor(void *arg) {
    TradingSystem *sys = (TradingSystem *)arg;
    while (*sys->running) {
        while (sem_trywait(&sys->queue.full) == 0) {
            pthread_mutex_lock(&sys->queue.mutex);
            Order order = sys->queue.orders[sys->queue.front];
            sys->queue.front = (sys->queue.front + 1) % QUEUE_SIZE;
            sys->queue.count--;
            pthread_mutex_unlock(&sys->queue.mutex);
            sem_post(&sys->queue.empty);
            handle_new_order(sys, order);
        }
        process_pending_orders(sys);
        usleep(50000);
    }
    return NULL;
}

void *order_execution_thread(void *arg) {
    return NULL; // retired, order_processor handles everything
}

// ─── Public functions ─────────────────────────────────────────────────────
int get_pending_orders(TradingSystem *sys, Order *snapshot, int max) {
    pthread_mutex_lock(&sys->pending_mutex);
    int count = sys->pending_count < max ? sys->pending_count : max;
    for (int i = 0; i < count; i++) snapshot[i] = sys->pending_orders[i];
    pthread_mutex_unlock(&sys->pending_mutex);
    return count;
}

int cancel_pending_order(TradingSystem *sys, int order_id) {
    pthread_mutex_lock(&sys->pending_mutex);
    for (int i = 0; i < sys->pending_count; i++) {
        if (sys->pending_orders[i].order_id == order_id &&
            sys->pending_orders[i].status   == PENDING) {
            release_order_resources(&sys->account, &sys->pending_orders[i]);
            add_to_history(sys, sys->pending_orders[i], CANCELLED);
            for (int j = i; j < sys->pending_count - 1; j++)
                sys->pending_orders[j] = sys->pending_orders[j + 1];
            sys->pending_count--;
            pthread_mutex_unlock(&sys->pending_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&sys->pending_mutex);
    return 0;
}

int place_secure_order(Account *acc, Order *new_order) {
    if (new_order->type == BUY && new_order->kind == MARKET) {
        // BUG FIX 2: Validate cash NOW before enqueuing, not later in handle_new_order.
        // Previously we returned 1 (success) immediately and the API sent "Order placed",
        // then handle_new_order silently rejected it 50ms later with no feedback to the user.
        // We don't know the exact market price here, so we use the current live price as
        // a best-effort check. handle_new_order will re-check with the actual price.
        Stock *stock = NULL;
        for (int i = 0; i < NUM_STOCKS; i++) {
            if (system_state.stocks[i].name[0] != '\0' &&
                strcmp(system_state.stocks[i].name, new_order->stock_name) == 0) {
                stock = &system_state.stocks[i];
                break;
            }
        }
        if (!stock) return 0;
        double estimated_cost = get_price(stock) * new_order->quantity;
        if (get_available_balance(acc) < estimated_cost) return 0;

    } else if (new_order->type == BUY && new_order->kind != MARKET) {
        // LIMIT/OCO buy: reserve the exact cash amount at the limit price
        double required = new_order->quantity * new_order->price;
        if (!reserve_cash(acc, required)) return 0;

    } else if (new_order->type == SELL && new_order->kind != MARKET) {
        // LIMIT/OCO sell: lock the shares so they can't be sold elsewhere
        if (get_available_holding(acc, new_order->stock_name) < new_order->quantity)
            return 0;
        lock_shares(acc, new_order->stock_name, new_order->quantity);

    } else if (new_order->type == SELL && new_order->kind == MARKET) {
        // BUG FIX 1: Use get_available_holding (unlocked shares only), NOT get_holding (total).
        // Previously if 10 shares were locked for a LIMIT SELL order, get_holding() still
        // returned 10, so a MARKET SELL for 10 would also go through — double-selling the
        // same shares. get_available_holding() returns quantity - locked_quantity.
        if (get_available_holding(acc, new_order->stock_name) < new_order->quantity)
            return 0;
    }

    new_order->status = PENDING;
    enqueue(&global_order_queue, *new_order);
    return 1;
}

void process_cancel_order(Account *acc, int order_id) {
    if (cancel_pending_order(&system_state, order_id)) return;

    pthread_mutex_lock(&global_order_queue.mutex);
    for (int i = 0; i < global_order_queue.count; i++) {
        int idx = (global_order_queue.front + i) % QUEUE_SIZE;
        Order *o = &global_order_queue.orders[idx];
        if (o->order_id == order_id && o->status == PENDING) {
            o->status = CANCELLED;
            pthread_mutex_unlock(&global_order_queue.mutex);
            release_order_resources(acc, o);
            add_to_history(&system_state, *o, CANCELLED);
            return;
        }
    }
    pthread_mutex_unlock(&global_order_queue.mutex);
}